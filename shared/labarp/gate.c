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
    int sockfdA, sockfdB; // Sockets para las interfaces A y B
    struct ifreq if_idxA, if_macA, if_idxB, if_macB; // Estructuras para obtener índices y MAC de las interfaces
    int i;
    ssize_t numbytes; // Cantidad de bytes recibidos
    byte recvbuf[BUF_SIZ], sendbuf[BUF_SIZ]; // Buffers para recepción y envío de datos
    struct sockaddr_ll socket_address; // Dirección del socket para enviar paquetes
    struct ether_header *eh = (struct ether_header *)recvbuf; // Cabecera Ethernet en el paquete recibido
    int saddr_size;
    struct sockaddr saddr;

    // Verificar si se proporcionaron las interfaces necesarias como argumentos
    if (argc != 3)
    {
        printf("Uso: %s INTERFAZ_A INTERFAZ_B\n", argv[0]);
        return 1;
    }

    // Crear sockets para las interfaces A y B
    if ((sockfdA = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1)
        perror("Socket A");

    if ((sockfdB = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1)
        perror("Socket B");

    // Configurar interfaz A
    memset(&if_idxA, 0, sizeof(struct ifreq));
    strncpy(if_idxA.ifr_name, argv[1], IFNAMSIZ - 1); // Nombre de la interfaz
    if (ioctl(sockfdA, SIOCGIFINDEX, &if_idxA) < 0) // Obtener índice de la interfaz
        perror("SIOCGIFINDEX A");

    memset(&if_macA, 0, sizeof(struct ifreq));
    strncpy(if_macA.ifr_name, argv[1], IFNAMSIZ - 1);
    if (ioctl(sockfdA, SIOCGIFHWADDR, &if_macA) < 0) // Obtener dirección MAC de la interfaz
        perror("SIOCGIFHWADDR A");

    // Configurar interfaz B
    memset(&if_idxB, 0, sizeof(struct ifreq));
    strncpy(if_idxB.ifr_name, argv[2], IFNAMSIZ - 1); // Nombre de la interfaz
    if (ioctl(sockfdB, SIOCGIFINDEX, &if_idxB) < 0) // Obtener índice de la interfaz
        perror("SIOCGIFINDEX B");

    memset(&if_macB, 0, sizeof(struct ifreq));
    strncpy(if_macB.ifr_name, argv[2], IFNAMSIZ - 1);
    if (ioctl(sockfdB, SIOCGIFHWADDR, &if_macB) < 0) // Obtener dirección MAC de la interfaz
        perror("SIOCGIFHWADDR B");

    // Mostrar configuración del gateway
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

    // Configurar para usar `select` para monitorear múltiples sockets
    fd_set readfds;
    int max_sd;

    // Inicializar el conjunto de descriptores de archivo
    FD_ZERO(&readfds);
    FD_SET(sockfdA, &readfds); // Agregar socket A
    FD_SET(sockfdB, &readfds); // Agregar socket B

    max_sd = (sockfdA > sockfdB) ? sockfdA : sockfdB; // Determinar el descriptor más alto

    // Esperar actividad en cualquiera de los sockets
    int activity = select(max_sd + 1, &readfds, NULL, NULL, NULL);
    if (activity < 0)
    {
        perror("select");
        return 1;
    }

    // Procesar datos del socket A si está activo
    if (FD_ISSET(sockfdA, &readfds))
    {
        saddr_size = sizeof(saddr);
        numbytes = recvfrom(sockfdA, recvbuf, BUF_SIZ, 0, &saddr, (socklen_t *)&saddr_size); // Recibir datos
        if (numbytes > 0)
        {
            // Verificar si el paquete no proviene de la misma interfaz
            if (memcmp(eh->ether_dhost, if_macA.ifr_hwaddr.sa_data, ETH_ALEN) != 0)
            {
                printf("Paquete recibido.\n");
                memcpy(sendbuf, recvbuf, numbytes); // Copiar datos al buffer de envío
                // Configurar destino y reenviar a subred B
                socket_address.sll_ifindex = if_idxB.ifr_ifindex;
                socket_address.sll_halen = ETH_ALEN;
                memcpy(socket_address.sll_addr, eh->ether_dhost, 6);

                sendto(sockfdB, sendbuf, numbytes, 0, (struct sockaddr *)&socket_address, sizeof(struct sockaddr_ll)); // Enviar datos
                printf("Paquete reenviado.\n");
            }
        }
    }

    // Procesar datos del socket B si está activo
    if (FD_ISSET(sockfdB, &readfds))
    {
        saddr_size = sizeof(saddr);
        numbytes = recvfrom(sockfdB, recvbuf, BUF_SIZ, 0, &saddr, (socklen_t *)&saddr_size); // Recibir datos
        if (numbytes > 0)
        {
            // Verificar si el paquete no proviene de la misma interfaz
            if (memcmp(eh->ether_dhost, if_macB.ifr_hwaddr.sa_data, ETH_ALEN) != 0)
            {
                printf("Respuesta recibida.\n");
                memcpy(sendbuf, recvbuf, numbytes); // Copiar datos al buffer de envío
                // Configurar destino y reenviar a subred A
                socket_address.sll_ifindex = if_idxA.ifr_ifindex;
                socket_address.sll_halen = ETH_ALEN;
                memcpy(socket_address.sll_addr, eh->ether_dhost, 6);

                sendto(sockfdA, sendbuf, numbytes, 0, (struct sockaddr *)&socket_address, sizeof(struct sockaddr_ll)); // Enviar datos
                printf("Respuesta reenviada.\n");
            }
        }
    }

    // Cerrar los sockets al finalizar
    close(sockfdA);
    close(sockfdB);

    return 0;
}
