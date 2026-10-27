/**
 * @file   lwipopts.h
 * @brief  LwIP 配置（验证构建用；CubeMX 工程请用其自动生成的同名文件）
 * @note   静态 IP（DHCP 由应用层可配）、netconn API、无 httpd（用自研 SMU_Web）。
 */
#ifndef __LWIPOPTS_H__
#define __LWIPOPTS_H__

#define NO_SYS                          0
#define LWIP_SOCKET                     1
#define LWIP_NETCONN                    1
#define LWIP_NETIF_API                  1
#define LWIP_NETIF_STATUS_CALLBACK      1
#define LWIP_NETIF_LINK_CALLBACK        1

#define SYS_LIGHTWEIGHT_PROT            1
#define MEM_ALIGNMENT                   4
#define MEM_SIZE                        16384
#define MEM_ALIGNMENT                   4
#define LWIP_COMPAT_MBOX                1   /* sys_mbox_valid 等映射到 mbox 函数 */
#define LWIP_COMPAT_SEM                 1
#define MEMP_NUM_PBUF                   16
#define MEMP_NUM_TCP_PCB                8
#define MEMP_NUM_TCP_PCB_LISTEN         4
#define MEMP_NUM_NETCONN                8
#define MEMP_NUM_NETBUF                 8
#define MEMP_NUM_SYS_TIMEOUT            8
#define PBUF_POOL_SIZE                  16
#define TCP_MSS                         1460
#define TCP_WND                         4096
#define TCP_SND_BUF                     4096
#define TCP_SND_QUEUELEN                8
#define TCP_QUEUE_OOSEQ                 0
#define PBUF_POOL_BUFSIZE               512

#define LWIP_TCP                        1
#define LWIP_UDP                        1
#define LWIP_ICMP                       1
#define LWIP_RAW                        1
#define LWIP_SO_SNDTIMEO                1
#define LWIP_SO_RCVTIMEO                1
#define LWIP_DHCP                       0
#define LWIP_AUTOIP                     0
#define LWIP_IGMP                       0
#define LWIP_DNS                        0
#define LWIP_UDP                        1
#define IP_SOF_BROADCAST                0
#define IP_SOF_BROADCAST_RECV           0
#define LWIP_HAVE_LOOPIF                0
#define LWIP_NETIF_LOOPBACK             0
#define TCP_LISTEN_BACKLOG              1
#define LWIP_TCPIP_CORE_LOCKING         1
#define LWIP_TCPIP_TIMEOUT              1
/* 注意：以下宏名必须与 LwIP opt.h 一致（曾因写成 LWIP_TCPIP_TASK_STACKSIZE
   导致 tcpip 线程栈大小为 0，pvPortMalloc(0) 失败进 malloc hook） */
#define TCPIP_THREAD_STACKSIZE          1024
#define TCPIP_THREAD_PRIO               3
#define TCPIP_MBOX_SIZE                 8
#define DEFAULT_ACCEPTMBOX_SIZE         4
#define DEFAULT_TCP_RECVMBOX_SIZE       4
#define DEFAULT_UDP_RECVMBOX_SIZE       4
#define DEFAULT_RAW_RECVMBOX_SIZE       4
#define LWIP_SINGLE_NETIF               1
#define LWIP_NETIF_HOSTNAME             1

#define LWIP_STATS                      0
#define LWIP_PROVIDE_ERRNO              1

#define CHECKSUM_BY_HARDWARE            1
#define LWIP_CHECKSUM_CTRL_PER_NETIF    1
#define ETH_PAD_SIZE                    2

#define MEMP_NUM_SYS_CALLBACKS          1

#endif /* __LWIPOPTS_H__ */
