#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <net/if.h>
#include <netinet/in.h>
#include <linux/if_packet.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/ip.h>
#include <netinet/tcp.h>
#include <netinet/ether.h>
#include <arpa/inet.h>
#include <linux/if.h>
#include <unistd.h>
#include <sys/mman.h>
#include <linux/if_ether.h>

#define TP_BLOCK_SIZE (1 << 17)
#define TP_FRAME_SIZE (1 << 11)
#define TP_BLOCK_NUM  10240
#define TP_FRAME_NUM  (TP_BLOCK_NUM*(TP_BLOCK_SIZE/TP_FRAME_SIZE))

static int setup_socket(u_char** ring, char* netdev)
{
    //create raw socket
    int fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (fd < 0) 
    {
        return -1;
    }

    //set to TPACKET_V2
    int ver = TPACKET_V2;
    int err = setsockopt(fd, SOL_PACKET, PACKET_VERSION, &ver, sizeof(ver));
    if (err < 0) 
    {
        close(fd);
        return -1;
    }

    //set ring param
    struct tpacket_req req;
    req.tp_block_size = TP_BLOCK_SIZE;
    req.tp_frame_size = TP_FRAME_SIZE;
    req.tp_block_nr   = TP_BLOCK_NUM;
    req.tp_frame_nr   = TP_FRAME_NUM; 
    err = setsockopt(fd, SOL_PACKET, PACKET_RX_RING, (void*)&req, sizeof(req));
    if (err < 0) 
    {
        close(fd);
        return -1;
    }

    //mmap to user buffer
    *ring = (u_char*)mmap(NULL, req.tp_block_size * req.tp_block_nr, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (*ring == MAP_FAILED) 
    {
        close(fd);
        return -1;
    }

    //bind to netdev
    struct sockaddr_ll ll;
    memset(&ll, 0, sizeof(ll));
    ll.sll_family = PF_PACKET;
    ll.sll_protocol = htons(ETH_P_ALL);
    ll.sll_ifindex = if_nametoindex(netdev);
    ll.sll_hatype = 0;
    ll.sll_pkttype = 0;
    ll.sll_halen = 0;
    err = bind(fd, (struct sockaddr*)&ll, sizeof(ll));
    if (err < 0) 
    {
        munmap(*ring, req.tp_block_size * req.tp_block_nr);
        close(fd);
        return -1;
    }

    return fd;
}

int main()
{
    //use cpu core 0
    //cpu_set_t mask;
    //CPU_ZERO(&mask);
    //CPU_SET(0, &mask);
    //pthread_setaffinity_np(pthread_self(), sizeof(mask), &mask);

    //setup raw socket and rings.
    u_char* ring;
    int fd = setup_socket(&ring, "eth0");
    if (fd == -1) return -1;

    //recv ring data
    int64_t recvBytes = 0;
    int32_t curFrameIndex = 0;
    while (true)
    {
        //定位到ring frame
        tpacket2_hdr* tpHdr = (tpacket2_hdr*)(ring + curFrameIndex * TP_FRAME_SIZE);
        if ((tpHdr->tp_status & TP_STATUS_USER) == 0)
        {
            struct pollfd pfd;
            pfd.fd = fd;
            pfd.revents = 0;
            pfd.events = POLLIN;
            poll(&pfd, 1, -1);
            continue;
        }

        //接收包长度
        recvBytes += tpHdr->tp_snaplen;

        //定位到网络frame
        u_char* recvBuf      = (uint8_t*)tpHdr + tpHdr->tp_mac;
        iphdr* pIPHeaderRecv = (iphdr*)(recvBuf + 14); //跳过frame头的mac\mac\type <6+6+2>        

        //解析数据帧        
        if (pIPHeaderRecv->protocol == IPPROTO_TCP)
        {
            //指向tcp header
            tcphdr* pTCPHeaderRecv = (tcphdr*)((char*)pIPHeaderRecv + pIPHeaderRecv->ihl * 4);

            //ip包总长度(ip header + tcp header + tcp data)
            int32_t ipPacketTotalLen = htons(pIPHeaderRecv->tot_len);

            //指向tcp data
            char *pRecvTCPData = (char*)pTCPHeaderRecv + pTCPHeaderRecv->th_off * 4;
            int32_t nRecvTCPDataLen = ipPacketTotalLen - pIPHeaderRecv->ihl * 4 - pTCPHeaderRecv->th_off * 4;


            char szRecvIPSrc[20], szRecvIpDest[20];           
            strncpy(szRecvIPSrc,  inet_ntoa(*(in_addr*)&pIPHeaderRecv->saddr), sizeof(szRecvIPSrc));
            strncpy(szRecvIpDest, inet_ntoa(*(in_addr*)&pIPHeaderRecv->daddr), sizeof(szRecvIpDest));

            printf("%s:%d->%s:%d, frame len: %d tcp data len: %d\n",
                szRecvIPSrc, ntohs(pTCPHeaderRecv->source), szRecvIpDest, ntohs(pTCPHeaderRecv->dest),
                tpHdr->tp_snaplen, nRecvTCPDataLen);
        }
        

        //处理完成，重新设置为内核可用状态
        tpHdr->tp_status = TP_STATUS_KERNEL;

        //处理下一个
        curFrameIndex = (curFrameIndex + 1) % TP_FRAME_NUM;

    }
  
    return 0;
}

//run logs.
//172.18.19.12:1158->172.18.112.99:52509, frame len : 416 tcp data len : 362
//172.18.19.12:2259->172.18.112.99:52512, frame len : 456 tcp data len : 392