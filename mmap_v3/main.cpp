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


struct block_desc {
    uint32_t version;
    uint32_t offset_to_priv;
    struct tpacket_hdr_v1 h1;
};

struct ring {
    struct iovec* rd;
    uint8_t* map;
    struct tpacket_req3 req;
};

#define MY_MMAP_BLOCK_NUM 10240

static int setup_socket(struct ring* ring, char* netdev)
{
    //create raw socket
    int fd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
    if (fd < 0) 
    {
        return -1;
    }

    //set v3
    int ver = TPACKET_V3;
    int err = setsockopt(fd, SOL_PACKET, PACKET_VERSION, &ver, sizeof(ver));
    if (err < 0) 
    {
        close(fd);
        return -1;
    }
    
    //config ring    
    unsigned int blocksiz = 1 << 18; //blocksize 至少 4096 一个页面大小
    unsigned int framesiz = 1 << 11; //2048 
    unsigned int blocknum = MY_MMAP_BLOCK_NUM;

    memset(&ring->req, 0, sizeof(ring->req));
    ring->req.tp_block_size = blocksiz;
    ring->req.tp_frame_size = framesiz;
    ring->req.tp_block_nr = blocknum;
    ring->req.tp_frame_nr = (blocksiz * blocknum) / framesiz;
    ring->req.tp_retire_blk_tov = 1; //1 ms
    ring->req.tp_feature_req_word = TP_FT_REQ_FILL_RXHASH;
    err = setsockopt(fd, SOL_PACKET, PACKET_RX_RING, &ring->req,  sizeof(ring->req));
    if (err < 0) 
    {
        close(fd);
        return -1;
    }

    //map to user buffer
    ring->map = (uint8_t*)mmap(NULL, ring->req.tp_block_size * ring->req.tp_block_nr,
        PROT_READ | PROT_WRITE, MAP_SHARED | MAP_LOCKED, fd, 0);
    if (ring->map == MAP_FAILED) 
    {
        close(fd);
        return -1;
    }

    ring->rd = (iovec*)malloc(ring->req.tp_block_nr * sizeof(*ring->rd));
    for (int i = 0; i < ring->req.tp_block_nr; ++i) 
    {
        ring->rd[i].iov_base = ring->map + (i * ring->req.tp_block_size);
        ring->rd[i].iov_len = ring->req.tp_block_size;
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
        munmap(ring->map, ring->req.tp_block_size * ring->req.tp_block_nr);
        free(ring->rd);
        close(fd);
        return -1;
    }

    return fd;
}


static void teardown_socket(struct ring* ring, int fd)
{
    munmap(ring->map, ring->req.tp_block_size * ring->req.tp_block_nr);
    free(ring->rd);
    close(fd);
}


int main()
{    
    struct ring ring;
    memset(&ring, 0, sizeof(ring));
    int fd = setup_socket(&ring, "eth0");
    if (fd == -1) return -1;    

    //recv data
    int64_t recvBytes = 0;
    unsigned int block_num = 0;
    while (true) 
    {
        struct block_desc* pbd = (struct block_desc*)ring.rd[block_num].iov_base;

        if ((pbd->h1.block_status & TP_STATUS_USER) == 0) 
        {
            struct pollfd pfd;
            pfd.fd = fd;
            pfd.events = POLLIN | POLLERR;
            pfd.revents = 0;
            poll(&pfd, 1, -1);
            continue;
        }

        //定位到块中的第一个数据包
        struct tpacket3_hdr* ppd = (struct tpacket3_hdr*)((uint8_t*)pbd + pbd->h1.offset_to_first_pkt);
        for (int i = 0; i < pbd->h1.num_pkts; i++) 
        {
            recvBytes += ppd->tp_snaplen;           

            //定位到网络frame
            u_char* recvBuf = (uint8_t*)ppd + ppd->tp_mac;
            iphdr* pIPHeaderRecv = (iphdr*)(recvBuf + 14); //跳过frame头的mac\mac\type <6+6+2>        

            //解析数据帧        
            if (pIPHeaderRecv->protocol == IPPROTO_TCP)
            {
                //指向tcp header
                tcphdr* pTCPHeaderRecv = (tcphdr*)((char*)pIPHeaderRecv + pIPHeaderRecv->ihl * 4);

                //ip包总长度(ip header + tcp header + tcp data)
                int32_t ipPacketTotalLen = htons(pIPHeaderRecv->tot_len);

                //指向tcp data
                char* pRecvTCPData = (char*)pTCPHeaderRecv + pTCPHeaderRecv->th_off * 4;
                int32_t nRecvTCPDataLen = ipPacketTotalLen - pIPHeaderRecv->ihl * 4 - pTCPHeaderRecv->th_off * 4;


                char szRecvIPSrc[20], szRecvIpDest[20];
                strncpy(szRecvIPSrc, inet_ntoa(*(in_addr*)&pIPHeaderRecv->saddr), sizeof(szRecvIPSrc));
                strncpy(szRecvIpDest, inet_ntoa(*(in_addr*)&pIPHeaderRecv->daddr), sizeof(szRecvIpDest));

                printf("%s:%d->%s:%d, frame len: %d tcp data len: %d\n",
                    szRecvIPSrc, ntohs(pTCPHeaderRecv->source), szRecvIpDest, ntohs(pTCPHeaderRecv->dest),
                    ppd->tp_snaplen, nRecvTCPDataLen);
            }


            ppd = (struct tpacket3_hdr*)((uint8_t*)ppd + ppd->tp_next_offset);
        }

        //处理完毕，释放块到内核可以用状态
        pbd->h1.block_status = TP_STATUS_KERNEL;

        block_num = (block_num + 1) % MY_MMAP_BLOCK_NUM;
    }

    teardown_socket(&ring, fd);

    return 0;
}

//run logs.
//172.18.19.12:1158->172.18.112.99:52509, frame len : 416 tcp data len : 362
//172.18.19.12:2259->172.18.112.99:52512, frame len : 456 tcp data len : 392