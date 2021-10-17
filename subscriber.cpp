#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "helpers.h"
#include <algorithm>
#include <unordered_map>
#include <queue>
#include <netinet/tcp.h>
#include <math.h>

using namespace std;

void usage(char *file)
{
	fprintf(stderr, "Usage: %s server_address server_port\n", file);
	exit(0);
}

int main(int argc, char *argv[])
{
	int sockfd, n, ret;
	struct sockaddr_in serv_addr;
    fd_set read_fds;	// multimea de citire folosita in select()
    fd_set tmp_fds;		// multime folosita temporar

    setvbuf(stdout, NULL, _IONBF, BUFSIZ);

	if (argc < 4) {
		usage(argv[0]);
    }

    // se completeaza perechea ip/port
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(atoi(argv[3]));
	ret = inet_aton(argv[2], &serv_addr.sin_addr);
	DIE(ret == 0, "inet_aton");
    
    //Se conecteaza la server
	sockfd = socket(AF_INET, SOCK_STREAM, 0);
	DIE(sockfd < 0, "socket");
	ret = connect(sockfd, (struct sockaddr*) &serv_addr, sizeof(serv_addr));
	DIE(ret < 0, "connect");

    //Se initializeaza multimile de file descriptors si se adauga stdin si socket ul server ului
    FD_ZERO(&read_fds);
    FD_ZERO(&tmp_fds);
    FD_SET(STDIN_FILENO, &read_fds);
	FD_SET(sockfd, &read_fds);

    //Se dezactiveaza algoritmul lui Neagle
    int Neagle_flag = 1;
	ret = setsockopt(sockfd, IPPROTO_TCP, TCP_NODELAY, &Neagle_flag, sizeof(int));
	DIE(ret < 0, "neagle");

    //Se trimite un mesaj cu ID-ul clientului
    TCP_message join{TCP_TYPE::JOIN};
    memcpy(join.topic, argv[1], 10);
    ret = send(sockfd, &join, sizeof(TCP_message), 0);

	while (1) {

        tmp_fds = read_fds;
        ret = select(sockfd + 1, &tmp_fds, NULL, NULL, NULL);
        DIE(ret < 0, "select");

        if(FD_ISSET(STDIN_FILENO, &tmp_fds)) {
            //se primesc date de la tastatura
            char buffer[MAX_BUFLEN];
            memset(buffer, 0, MAX_BUFLEN);
            // se citeste de la tastatura
            fgets(buffer, MAX_BUFLEN - 1, stdin);
            TCP_message m{};
            char *tok = strtok(buffer, " \n");
            if(!tok) {
                perror("Wrong command!\n");
                continue;
            }
            if(strcmp(tok, "subscribe") == 0) {
                m.tip = TCP_TYPE::SUBSCRIBE;
            } else if(strcmp(tok, "unsubscribe") == 0) {
                m.tip = TCP_TYPE::UNSUBSCRIBE;
            } else if(strcmp(tok, "exit") == 0) {
                m.tip = TCP_TYPE::EXIT;
                ret = send(sockfd, &m, sizeof(TCP_message), 0);
                DIE(ret < 0, "send_TCPexit");
                break;
            } else {
                perror("Wrong command!\n");
                continue;
            }
            tok = strtok(NULL, " \n");
            if(!tok) {
                perror("Wrong command!\n");
                continue;
            }
            memcpy(m.topic, tok, TOPIC_LEN);
            tok = strtok(NULL, " \n");
            if(tok) {
                if(tok[0] == '1') {
                    m.sf = true;
                }
                else if(tok[0] == '0') {
                    m.sf = false;
                }
                else {
                    perror("Wrong command!\n");
                    continue;
                }
            } else {
                perror("Wrong command!\n");
                continue;
            }
            //Se trimite mesajul TCP serverului
            ret = send(sockfd, &m, sizeof(TCP_message), 0);
            DIE(ret < 0, "send_TCPACTION");
            if(strcmp(buffer, "subscribe") == 0) {
                printf("Subscribed to topic.\n");
            }
            else {
                printf("Unubscribed from topic.\n");
            }
        }

        if(FD_ISSET(sockfd, &tmp_fds)) {
            UDP_message m{};
            uint32_t modul_float;
            uint8_t putere_float;
            //Se citeste primul int, care reprezinta lungimea continutului
            ret = recv(sockfd, &m, sizeof(int), 0);
            //Se citesc apoi atatia octeti cat e necesar pentru o transmitere eficienta a datelor din continut
            ret = recv(sockfd, (char*) &m + sizeof(int), offsetof(UDP_message, continut) - sizeof(int) + m.content_len, 0);
            DIE(ret < 0, "recv");
            //Daca server ul s-a inchis, clientul primeste acest mesaj
            if(m.tip_date == UDP_TYPE_KICK) {
                break;
            } else {
                //Se contruieste un string in functie de tipul datei trimise
                string msg_builder = string(inet_ntoa(m.from.sin_addr)) + ':' + to_string(htons(m.from.sin_port))
                    + " - " + string(m.topic);
                switch (m.tip_date) {
                    case UDP_TYPE_INT:
                        msg_builder += " - INT - ";
                        if(m.continut[0] == 1) {
                            msg_builder += "-";
                        }
                        msg_builder += to_string(ntohl(*(uint32_t*)(m.continut + sizeof(char))));
                        break;
                    
                    case UDP_TYPE_SHORT:
                        msg_builder += " - SHORT_REAL - " + to_string((float) ntohs(*(uint16_t*) m.continut) / 100.00 );
                        break;

                    case UDP_TYPE_FLOAT:
                        msg_builder += " - FLOAT - ";
                        if(m.continut[0] == 1) {
                            msg_builder += "-";
                        }
                        modul_float = ntohl(*(uint32_t*)(m.continut + sizeof(char)));
                        putere_float = *(uint8_t*)(m.continut + sizeof(char) + sizeof(uint32_t));
                        msg_builder += to_string((double)modul_float / pow(10, putere_float));
                        break;

                    case UDP_TYPE_STRING:
                        msg_builder += " - STRING - " + string(m.continut);
                }
                //Se afiseaza mesajul construit
                printf("%s\n", msg_builder.c_str());
            }
        }

	}

	close(sockfd);

	return 0;
}
