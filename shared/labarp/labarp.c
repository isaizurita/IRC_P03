#include "eth.h"
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <string.h>
#include <arpa/inet.h>
#include <net/ethernet.h>
#include <stdio.h>

#define TYPE_REQUEST 0x01 // Tipo de mensaje: solicitud
#define TYPE_RESPONSE 0x02 // Tipo de mensaje: respuesta
#define ETH_ALEN 6 // Longitud de la dirección MAC (6 bytes)

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
    int sockfd; // Descriptor de socket
    struct ifreq if_idx, if_mac; // Estructuras para información de interfaz y MAC
    struct sockaddr_ll socket_address; // Dirección del socket
    struct ether_header *eh; // Cabecera Ethernet
    char sendbuf[BUF_SIZ], recvbuf[BUF_SIZ]; // Buffers para enviar y recibir
    ssize_t numbytes; // Cantidad de bytes recibidos
    pid_t pid; // ID del proceso para fork
    int saddr_size; // Tamaño de la dirección del socket
    struct sockaddr saddr; // Dirección del socket para recepción

    // Validar argumentos de entrada
    if (argc != 4)
    {
        printf("Uso: %s INTERFAZ NOMBRE SUBRED\n", argv[0]);
        return 1;
    }

    char *iface = argv[1]; // Nombre de la interfaz
    char *name = argv[2]; // Nombre del nodo
    char *subnet = argv[3]; // Subred a la que pertenece el nodo

    // Inicialización del socket
    if ((sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1)
    {
        perror("socket");
        return 1;
    }

    // Obtener el índice de la interfaz
    memset(&if_idx, 0, sizeof(struct ifreq));
    strncpy(if_idx.ifr_name, iface, IFNAMSIZ - 1);
    if (ioctl(sockfd, SIOCGIFINDEX, &if_idx) < 0)
    {
        perror("SIOCGIFINDEX");
        return 1;
    }

    // Obtener la dirección MAC de la interfaz
    memset(&if_mac, 0, sizeof(struct ifreq));
    strncpy(if_mac.ifr_name, iface, IFNAMSIZ - 1);
    if (ioctl(sockfd, SIOCGIFHWADDR, &if_mac) < 0)
    {
        perror("SIOCGIFHWADDR");
        return 1;
    }

    unsigned char *local_mac = (unsigned char *)if_mac.ifr_hwaddr.sa_data;

    // Mostrar información de la interfaz
    printf("Interfaz: %s, MAC: %02x:%02x:%02x:%02x:%02x:%02x\n",
           iface,
           local_mac[0], local_mac[1], local_mac[2], local_mac[3], local_mac[4], local_mac[5]);

    // Crear un proceso hijo usando fork
    pid = fork();
    if (pid != 0)
    {
        // Proceso padre: Envío de mensajes
        char dest_name[MAX_NAME_LEN];

        while (1)
        {
            printf("Destino (exit para salir): ");
            fgets(dest_name, MAX_NAME_LEN, stdin);
            dest_name[strcspn(dest_name, "\n")] = 0; // Remover salto de línea

            if (strcmp(dest_name, "exit") == 0)
            {
                kill(pid, SIGKILL); // Terminar proceso hijo
                break;
            }

            // Preparar mensaje
            memset(sendbuf, 0, BUF_SIZ);
            eh = (struct ether_header *)sendbuf;

            // Configurar cabecera Ethernet
            memcpy(eh->ether_shost, local_mac, 6); // Dirección origen
            memset(eh->ether_dhost, 0xff, 6); // Broadcast
            eh->ether_type = htons(0x1234); // Tipo personalizado

            // Generar el ID único para el mensaje
            unsigned char msg_id = message_id++;

            // Agregar datos al mensaje
            int offset = sizeof(struct ether_header);
            sendbuf[offset] = TYPE_REQUEST; // Tipo de mensaje
            offset += 1;
            sendbuf[offset] = msg_id; // ID del mensaje
            offset += 1;
            strcpy(sendbuf + offset, name); // Nombre del origen
            offset += strlen(name) + 1;
            strcpy(sendbuf + offset, dest_name); // Nombre del destino
            offset += strlen(dest_name) + 1;

            // Configurar dirección del socket
            socket_address.sll_ifindex = if_idx.ifr_ifindex;
            socket_address.sll_halen = ETH_ALEN;
            memset(socket_address.sll_addr, 0xff, 6); // Dirección de broadcast

            // Enviar el mensaje
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

                // Ignorar paquetes propios
                if (memcmp(eh->ether_shost, local_mac, 6) == 0)
                {
                    continue;
                }

                // Procesar mensaje recibido
                int offset = sizeof(struct ether_header);
                unsigned char msg_type = recvbuf[offset]; // Tipo de mensaje
                offset += 1;

                unsigned char msg_id = recvbuf[offset]; // ID del mensaje
                offset += 1;

                char sender[MAX_NAME_LEN], recipient[MAX_NAME_LEN];
                strcpy(sender, recvbuf + offset); // Nombre del emisor
                offset += strlen(sender) + 1;
                strcpy(recipient, recvbuf + offset); // Nombre del receptor
                offset += strlen(recipient) + 1;

                // Verificar si el mensaje es para este nodo
                if (strcmp(recipient, name) == 0)
                {
                    if (msg_type == TYPE_REQUEST)
                    {
                        printf("Solicitud recibida de %s. Respondiendo...\n", sender);

                        // Verificar si ya se respondió a este mensaje
                        if (!has_already_responded(msg_id))
                        {
                            // Preparar respuesta
                            memcpy(eh->ether_dhost, eh->ether_shost, 6); // Dirección destino
                            memcpy(eh->ether_shost, local_mac, 6); // Dirección origen

                            recvbuf[sizeof(struct ether_header)] = TYPE_RESPONSE; // Cambiar tipo a respuesta
                            recvbuf[sizeof(struct ether_header) + 1] = msg_id; // Incluir ID en la respuesta
                            strcpy(recvbuf + sizeof(struct ether_header) + 2, name); // Nombre del origen
                            strcpy(recvbuf + sizeof(struct ether_header) + 2 + strlen(name) + 1, sender); // Nombre del destino

                            // Enviar la respuesta
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

    // Cerrar el socket
    close(sockfd);
    return 0;
}
