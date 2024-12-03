#include "eth.h"
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <string.h>
#include <arpa/inet.h>
#include <net/ethernet.h>
#include <stdio.h>

#define TYPE_REQUEST 0x01
#define TYPE_RESPONSE 0x02
#define ETH_ALEN 6

// Agregar un ID único a cada mensaje
static int message_id = 0;

// Lista para almacenar los IDs de mensajes recibidos
static int received_ids[100];
static int received_count = 0;

// Función para verificar si el ID del mensaje ya ha sido procesado
int has_already_responded(int msg_id) {
    for (int i = 0; i < received_count; i++) {
        if (received_ids[i] == msg_id) {
            return 1; // Ya ha sido procesado
        }
    }
    return 0; // No ha sido procesado
}

int main(int argc, char *argv[])
{
    int sockfd;
    struct ifreq if_idx, if_mac;
    struct sockaddr_ll socket_address;
    struct ether_header *eh;
    char sendbuf[BUF_SIZ], recvbuf[BUF_SIZ];
    ssize_t numbytes;
    pid_t pid;
    int saddr_size;
    struct sockaddr saddr;

    if (argc != 4)
    {
        printf("Uso: %s INTERFAZ NOMBRE SUBRED\n", argv[0]);
        return 1;
    }

    char *iface = argv[1];
    char *name = argv[2];
    char *subnet = argv[3];

    // Inicialización del socket
    if ((sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1)
    {
        perror("socket");
        return 1;
    }

    memset(&if_idx, 0, sizeof(struct ifreq));
    strncpy(if_idx.ifr_name, iface, IFNAMSIZ - 1);
    if (ioctl(sockfd, SIOCGIFINDEX, &if_idx) < 0)
    {
        perror("SIOCGIFINDEX");
        return 1;
    }

    memset(&if_mac, 0, sizeof(struct ifreq));
    strncpy(if_mac.ifr_name, iface, IFNAMSIZ - 1);
    if (ioctl(sockfd, SIOCGIFHWADDR, &if_mac) < 0)
    {
        perror("SIOCGIFHWADDR");
        return 1;
    }

    unsigned char *local_mac = (unsigned char *)if_mac.ifr_hwaddr.sa_data;

    printf("Interfaz: %s, MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
           iface,
           local_mac[0], local_mac[1], local_mac[2], local_mac[3], local_mac[4], local_mac[5]);

    pid = fork();
    if (pid != 0)
    {
        // Proceso padre: Envío de mensajes
        char dest_name[MAX_NAME_LEN];

        while (1)
        {
            printf("Destino (exit para salir): ");
            fgets(dest_name, MAX_NAME_LEN, stdin);
            dest_name[strcspn(dest_name, "\n")] = 0;

            if (strcmp(dest_name, "exit") == 0)
            {
                kill(pid, SIGKILL);
                break;
            }

            // Preparar mensaje
            memset(sendbuf, 0, BUF_SIZ);
            eh = (struct ether_header *)sendbuf;

            memcpy(eh->ether_shost, local_mac, 6);
            memset(eh->ether_dhost, 0xff, 6); // Broadcast
            eh->ether_type = htons(0x1234);  // Tipo personalizado

            // Generar el ID único para el mensaje
            unsigned char msg_id = message_id++;

            // Agregar datos
            int offset = sizeof(struct ether_header);
            sendbuf[offset] = TYPE_REQUEST;
            offset += 1;
            sendbuf[offset] = msg_id; // Agregar ID al mensaje
            offset += 1;
            strcpy(sendbuf + offset, name); // Origen
            offset += strlen(name) + 1;
            strcpy(sendbuf + offset, dest_name); // Destino
            offset += strlen(dest_name) + 1;

            // Enviar
            socket_address.sll_ifindex = if_idx.ifr_ifindex;
            socket_address.sll_halen = ETH_ALEN;
            memset(socket_address.sll_addr, 0xff, 6); // Broadcast

            if (sendto(sockfd, sendbuf, offset, 0, (struct sockaddr *)&socket_address, sizeof(socket_address)) < 0)
            {
                perror("sendto");
            }
            else
            {
                printf("Mensaje enviado a %s.\n", dest_name);
            }
        }
    }
    else
    {
        // Proceso hijo: Recepción de mensajes
        while (1)
        {
            saddr_size = sizeof(saddr);
            numbytes = recvfrom(sockfd, recvbuf, BUF_SIZ, 0, &saddr, (socklen_t *)&saddr_size);

            if (numbytes > 0)
            {
                eh = (struct ether_header *)recvbuf;
                if (memcmp(eh->ether_shost, local_mac, 6) == 0)
                {
                    continue; // Ignorar paquetes propios
                }

                int offset = sizeof(struct ether_header);
                unsigned char msg_type = recvbuf[offset];
                offset += 1;

                unsigned char msg_id = recvbuf[offset];  // ID del mensaje
                offset += 1;

                char sender[MAX_NAME_LEN], recipient[MAX_NAME_LEN];
                strcpy(sender, recvbuf + offset);
                offset += strlen(sender) + 1;
                strcpy(recipient, recvbuf + offset);
                offset += strlen(recipient) + 1;

                if (strcmp(recipient, name) == 0)
                {
                    if (msg_type == TYPE_REQUEST)
                    {
                        printf("Solicitud recibida de %s. Respondiendo...\n", sender);

                        // Verificar si ya se respondió a este mensaje
                        if (!has_already_responded(msg_id))
                        {
                            // Procesar y responder
                            memcpy(eh->ether_dhost, eh->ether_shost, 6);
                            memcpy(eh->ether_shost, local_mac, 6);

                            recvbuf[sizeof(struct ether_header)] = TYPE_RESPONSE;
                            recvbuf[sizeof(struct ether_header) + 1] = msg_id; // Incluir ID en la respuesta
                            strcpy(recvbuf + sizeof(struct ether_header) + 2, name);
                            strcpy(recvbuf + sizeof(struct ether_header) + 2 + strlen(name) + 1, sender);

                            if (sendto(sockfd, recvbuf, numbytes, 0, &saddr, saddr_size) < 0)
                            {
                                perror("sendto");
                            }

                            // Registrar el ID como procesado
                            received_ids[received_count++] = msg_id;
                        }
                    }
                    else if (msg_type == TYPE_RESPONSE)
                    {
                        printf("Respuesta recibida de %s.\n", sender);
                    }
                }
            }
        }
    }

    close(sockfd);
    return 0;
}
