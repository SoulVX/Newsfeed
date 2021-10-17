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

using namespace std;

void usage(char *file) {
	fprintf(stderr, "Usage: %s server_port\n", file);
	exit(0);
}

int main(int argc, char *argv[])
{
	int sockfdTCP, sockfdUDP, newsockfd, portno;
	struct sockaddr_in serv_addr, cli_addr;
	int i, ret;
	socklen_t socketlen = sizeof(serv_addr);

	setvbuf(stdout, NULL, _IONBF, BUFSIZ);

	//Pentru fiecare socket, pastrez ID ul clientului conectat pe socket
	unordered_map<int, string> socket__ID;
	//Pentru fiecare ID client, pastrez socket-ul pe care acesta e conectat
	unordered_map<string, int> ID__socket;
	//Pentru fiecare topic, pastrez ID-urile si alegerea SF a clientilor abonati la topicul respectiv
	unordered_map<string, unordered_map<string, bool>> topic__ID_SF;   // {topic1 - {ID1-bool1, ID2 - bool2, ...}, topic2 - ...}
	//Pentru fiecare ID client, pastrez daca clientul cu acel ID este online sau nu
	unordered_map<string, bool> ID__online;
	//Pentru fiecare ID client, pastez coada de mesaje pe care le-a primit cat a fost offline
	unordered_map<string, queue<UDP_message>> ID__queue;

	// multimea de citire folosita in select()
	fd_set read_fds;
	// multime folosita temporar
	fd_set tmp_fds;
	// valoare maxima fd din multimea read_fds
	int fdmax;			

	if (argc != 2) {
		usage(argv[0]);
	}

	// se completeaza perechea ip/port
	portno = atoi(argv[1]);
	DIE(portno == 0, "server_atoi");
	memset((char *) &serv_addr, 0, socketlen);
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_port = htons(portno);
	serv_addr.sin_addr.s_addr = INADDR_ANY;

	// 	se face socketul TCP, se leaga la ip/port si se face pasiv
	sockfdTCP = socket(AF_INET, SOCK_STREAM, 0);
	DIE(sockfdTCP < 0, "TCP_socket");
	ret = bind(sockfdTCP, (struct sockaddr *) &serv_addr, socketlen);
	DIE(ret < 0, "TCP_bind");
	ret = listen(sockfdTCP, 10);
	DIE(ret < 0, "TCP_listen");

	// 	se face socketul UDP, se leaga la ip/port si se face pasiv
	sockfdUDP = socket(AF_INET, SOCK_DGRAM, 0);
	DIE(sockfdUDP < 0, "UDP_socket");
	ret = bind(sockfdUDP, (struct sockaddr *) &serv_addr, socketlen);
	DIE(ret < 0, "UDP_bind");

	//Se initializeaza multimile de sockets
	FD_ZERO(&read_fds);
	FD_ZERO(&tmp_fds);
	//Se adauga socket ul de UDP si de TCP (pasive) si intrarea de la tastatura
	FD_SET(sockfdTCP, &read_fds);
	FD_SET(sockfdUDP, &read_fds);
	FD_SET(STDIN_FILENO, &read_fds);
	fdmax = max(sockfdUDP, sockfdTCP);

	//Se dezactiveaza algoritmul lui Neagle
	int Neagle_flag = 1;
	ret = setsockopt(sockfdTCP, IPPROTO_TCP, TCP_NODELAY, &Neagle_flag, sizeof(int));
	DIE(ret < 0, "neagle");

	while (1) {
		tmp_fds = read_fds; 
		
		//Se aleg sockets pe care au venit date
		ret = select(fdmax + 1, &tmp_fds, NULL, NULL, NULL);
		DIE(ret < 0, "select");
		
		for (i = 0; i <= fdmax; i++) {
			//Daca i e in multimea in care s-a scris
			if (FD_ISSET(i, &tmp_fds)) {
				if (i == sockfdTCP) {
					// cerere conectare client TCP
					socklen_t clilen = sizeof(cli_addr);
					newsockfd = accept(sockfdTCP, (struct sockaddr *) &cli_addr, &clilen);
					DIE(newsockfd < 0, "accept_new_TCP_client");
					// se primeste ID-ul noului client
					TCP_message join{};
					ret = recv(newsockfd, &join, sizeof(TCP_message), 0);
					DIE(ret < 0, "TCP_join");
					DIE(join.tip != TCP_TYPE::JOIN, "wrong_join_message");

					string id = string(join.topic);
					if(ID__online[id]) {
						//Un client cu acelasi ID e online, nu se permite conectarea
						UDP_message kick_message{};
						kick_message.tip_date = 4;
						ret = send(newsockfd, &kick_message, sizeof(UDP_message), 0);
						DIE(ret < 0, "TCP_kick");
						printf("Client %s already connected.\n", join.topic);
						close(newsockfd);
						continue;
					} else {
						//Se trimit mesajele (daca exista) aflate in coada clientului ID
						while(!ID__queue[id].empty()) {
							ret = send(newsockfd, &(ID__queue[id].front()), offsetof(UDP_message, continut) + ID__queue[id].front().content_len, 0);
							DIE(ret < 0, "queue_send");
							ID__queue[id].pop();
						}
					}

					//Se actualizeaza datele clientului conectat
					socket__ID[newsockfd] = id;
					ID__socket[id] = newsockfd;
					ID__online[id] = true;

					//Se pune noul socket in multimea de socketi pe care se pot primi date
					FD_SET(newsockfd, &read_fds);
					if (newsockfd > fdmax)
						fdmax = newsockfd;

					//Se afiseaza mesajul cerut pe consola server
					printf("New client %s connected from %s:%d\n", join.topic, inet_ntoa(cli_addr.sin_addr), ntohs(cli_addr.sin_port));

					continue;

				} else if(i == sockfdUDP) {
					// mesaj de la clientii UDP
					UDP_message m{};
					sockaddr_in from_station;
					socklen_t socklen;
					//Se completeaza topic, tip_date si continut cu datele primite de la clientii UDP
					ret = recvfrom(sockfdUDP, (char*) &m + sizeof(sockaddr_in) + sizeof(int), sizeof(UDP_message) - sizeof(sockaddr_in), 0, (struct sockaddr *) &from_station, &socklen);
					DIE(ret < 0, "recvfrom_UDP");
					//Se completeaza from cu informatia despre sender primita din recvfrom
					m.from = from_station;
					//Pentru fiecare dip de date, se calculeaza lungimea adevarata a continutului
					switch (m.tip_date)
						{
						case UDP_TYPE_INT:
							m.content_len = 1 + sizeof(uint32_t) + 5;
							break;
							
						case UDP_TYPE_SHORT:
							m.content_len = sizeof(uint16_t);
							break;

						case UDP_TYPE_FLOAT:
							m.content_len = 1 + sizeof(uint32_t) + sizeof(uint8_t);
							break;

						case UDP_TYPE_STRING:
							m.content_len = strlen(m.continut);
							break;
						}
						m.content_len++;	
					for(auto& it : topic__ID_SF[string(m.topic)]) {
						//se trimite mesajul mai departe la subscriberii topicului daca sunt online,
						if(ID__online[it.first]) {
							//Se trimit clientilor tot continutul struct-ului pana la m.client, si inca m.content_len bytes in care se gaseste contentul
							ret = send(ID__socket[it.first], &m, offsetof(UDP_message, continut) + m.content_len, 0);
							DIE(ret < 0, "send_UPD_to_clients");
						} else if(it.second) {
							//daca nu, li se pun in coada SF daca au selectat acest lucru
							ID__queue[it.first].push(m);
						}
					}
					continue;
				} else if(i == 0) {
					// comanda de la tastatura, se citeste
					char buffer[MAX_BUFLEN];
					memset(buffer, 0, MAX_BUFLEN);
					fgets(buffer, MAX_BUFLEN, stdin);

					if (strcmp(buffer, "exit\n") == 0) {
						UDP_message kick_message{};
						kick_message.tip_date = UDP_TYPE_KICK;
						
						//Se trimite fiecarui client conectat un pachet KICK si i se inchide socket-ul
                		for (int k = 5; k <= fdmax; k++) {
                   			 if (FD_ISSET(k, &read_fds)) {
                        		ret = send(k, &kick_message, sizeof(UDP_message), 0);
								DIE(ret < 0, "all_kick");
								close(k);
                    		}
                		}
						close(sockfdTCP);
						close(sockfdUDP);
						return 0;
            		} else {
						perror("Unknown command.\n");
						continue;
					}
				} else {
					// mesaj de la clientii TCP (subscribe / unsubscribe / exit )
					TCP_message m{};
					ret = recv(i, &m, sizeof(TCP_message), 0);
					DIE(ret < 0, "recv_TCP_message");
					string id = string(socket__ID[i]);
					string topic = string(m.topic);

					if (m.tip == TCP_TYPE::EXIT) {
						// conexiunea s-a inchis
						ID__online[id] = false;
						ID__socket.erase(id);
						socket__ID.erase(i);
						printf("Client %s disconnected.\n", id.c_str());
						close(i);
						
						// se elimina socket ul inchis din SET si se actualizeaza fdmax
						FD_CLR(i, &read_fds);
                        for (int j = fdmax; j > 2; j--) {
                            if (FD_ISSET(j, &read_fds)) {
                                fdmax = j;
                                break;
                            }
                        }
					} else if(m.tip == TCP_TYPE::SUBSCRIBE) {
						topic__ID_SF[topic][id] = m.sf;
					} else {
						//Daca exista topicul respectiv
						if(topic__ID_SF.find(topic) != topic__ID_SF.end()) {
							topic__ID_SF[topic].erase(id);
						}
					} 
				}
			}
		}
	}

	return 0;
}
