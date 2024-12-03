#include "eth.h"
#include <unistd.h>
#include <sys/ioctl.h>
#include <signal.h>
#include <sys/types.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>

#define ETH_ALEN 6
#define SUBNET_A 1
#define SUBNET_B 2

int main(int argc, char *argv[])
{
    int sockfdA, sockfdB;
    struct ifreq if_idxA, if_macA, if_idxB, if_macB;
    int i;
    ssize_t numbytes;
    byte recvbuf[BUF_SIZ], sendbuf[BUF_SIZ];
    struct sockaddr_ll socket_address;
    struct ether_header *eh = (struct ether_header *)recvbuf;
    int saddr_size;
    struct sockaddr saddr;

    if (argc != 3)
    {
        printf("Uso: %s INTERFAZ_A INTERFAZ_B\n", argv[0]);
        return 1;
    }

    // Crear sockets para ambas interfaces
    if ((sockfdA = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1)
        perror("Socket A");

    if ((sockfdB = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1)
        perror("Socket B");

    // Configurar interfaz A
    memset(&if_idxA, 0, sizeof(struct ifreq));
    strncpy(if_idxA.ifr_name, argv[1], IFNAMSIZ - 1);
    if (ioctl(sockfdA, SIOCGIFINDEX, &if_idxA) < 0)
        perror("SIOCGIFINDEX A");

    memset(&if_macA, 0, sizeof(struct ifreq));
    strncpy(if_macA.ifr_name, argv[1], IFNAMSIZ - 1);
    if (ioctl(sockfdA, SIOCGIFHWADDR, &if_macA) < 0)
        perror("SIOCGIFHWADDR A");

    // Configurar interfaz B
    memset(&if_idxB, 0, sizeof(struct ifreq));
    strncpy(if_idxB.ifr_name, argv[2], IFNAMSIZ - 1);
    if (ioctl(sockfdB, SIOCGIFINDEX, &if_idxB) < 0)
        perror("SIOCGIFINDEX B");

    memset(&if_macB, 0, sizeof(struct ifreq));
    strncpy(if_macB.ifr_name, argv[2], IFNAMSIZ - 1);
    if (ioctl(sockfdB, SIOCGIFHWADDR, &if_macB) < 0)
        perror("SIOCGIFHWADDR B");

    printf("Gateway configurado con:\n");
    printf("Interfaz A (subred A): %s, MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
           argv[1],
           (byte)(if_macA.ifr_hwaddr.sa_data[0]), (byte)(if_macA.ifr_hwaddr.sa_data[1]),
           (byte)(if_macA.ifr_hwaddr.sa_data[2]), (byte)(if_macA.ifr_hwaddr.sa_data[3]),
           (byte)(if_macA.ifr_hwaddr.sa_data[4]), (byte)(if_macA.ifr_hwaddr.sa_data[5]));

    printf("Interfaz B (subred B): %s, MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
           argv[2],
           (byte)(if_macB.ifr_hwaddr.sa_data[0]), (byte)(if_macB.ifr_hwaddr.sa_data[1]),
           (byte)(if_macB.ifr_hwaddr.sa_data[2]), (byte)(if_macB.ifr_hwaddr.sa_data[3]),
           (byte)(if_macB.ifr_hwaddr.sa_data[4]), (byte)(if_macB.ifr_hwaddr.sa_data[5]));

    fd_set readfds;
    int max_sd;

    // Preparar el conjunto de descriptores de archivo para `select`
    FD_ZERO(&readfds);
    FD_SET(sockfdA, &readfds);
    FD_SET(sockfdB, &readfds);

    // Establecer el descriptor máximo
    max_sd = (sockfdA > sockfdB) ? sockfdA : sockfdB;

    // Usar `select` para esperar datos en ambos sockets
    int activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
    if (activity < 0)
    {
        perror("select");
        return 1;
    }

    // Si hay datos disponibles en el socket A
    if (FD_ISSET(sockfdA, &readfds))
    {
        saddr_size = sizeof(saddr);
        numbytes = recvfrom(sockfdA, recvbuf, BUF_SIZ, 0, &saddr, (socklen_t *)&saddr_size);
        if (numbytes > 0)
        {
            // Verificar que el paquete no provenga de la misma interfaz
            if (memcmp(eh->ether_dhost, if_macA.ifr_hwaddr.sa_data, ETH_ALEN) != 0)
            {
                printf("Recibido paquete en subred A.\n");
                memcpy(sendbuf, recvbuf, numbytes);
                // Enviar a subred B
                socket_address.sll_ifindex = if_idxB.ifr_ifindex;
                socket_address.sll_halen = ETH_ALEN;
                memcpy(socket_address.sll_addr, eh->ether_dhost, 6);

                sendto(sockfdB, sendbuf, numbytes, 0, (struct sockaddr *)&socket_address, sizeof(struct sockaddr_ll));
                printf("Paquete reenviado a subred B.\n");
            }
        }
    }

    // Si hay datos disponibles en el socket B
    if (FD_ISSET(sockfdB, &readfds))
    {
        saddr_size = sizeof(saddr);
        numbytes = recvfrom(sockfdB, recvbuf, BUF_SIZ, 0, &saddr, (socklen_t *)&saddr_size);
        if (numbytes > 0)
        {
            // Verificar que el paquete no provenga de la misma interfaz
            if (memcmp(eh->ether_dhost, if_macB.ifr_hwaddr.sa_data, ETH_ALEN) != 0)
            {
                printf("Recibido paquete en subred B.\n");
                memcpy(sendbuf, recvbuf, numbytes);
                // Enviar a subred A
                socket_address.sll_ifindex = if_idxA.ifr_ifindex;
                socket_address.sll_halen = ETH_ALEN;
                memcpy(socket_address.sll_addr, eh->ether_dhost, 6);

                sendto(sockfdA, sendbuf, numbytes, 0, (struct sockaddr *)&socket_address, sizeof(struct sockaddr_ll));
                printf("Paquete reenviado a subred A.\n");
            }
        }
    }

    // Cerrar los sockets después de procesar un solo par de mensajes
    close(sockfdA);
    close(sockfdB);

    return 0;
}
