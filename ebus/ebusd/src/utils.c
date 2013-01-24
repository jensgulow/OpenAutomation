/*
 * Copyright (C) Roland Jax 2012-2013 <roland.jax@liwest.at>
 *
 * This file is part of ebusd.
 *
 * ebusd is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * ebusd is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with ebusd. If not, see http://www.gnu.org/licenses/.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>

#include "log.h"
#include "ebus.h"
#include "utils.h"




void
debug_ebus_msg(unsigned char buf[], int buflen, int nosyn)
{

	if (nosyn == NO || buflen > 1) {
		int k = 0;
		char msg[SERIAL_BUFSIZE];
		char tmp[4];

		memset(tmp, '\0', sizeof(tmp));
		memset(msg, '\0', sizeof(msg));

		for (k = 0; k <= buflen; k++) {
			sprintf(tmp, " %02x", buf[k]);
			strncat(msg, tmp, 3);
		}
		log_print_msg(DBG, "%s", msg);
	}
}

int
serial_ebus_get_msg(int fd, unsigned char buf[], int *buflen,
							int rawdump, int nosyn)
{
	static unsigned char msgbuf[SERIAL_BUFSIZE];
	static int msglen = 0;
	int maxlen, i;

	if (msglen == 0)
		memset(msgbuf, '\0', sizeof(msgbuf));


	maxlen = *buflen;

	*buflen = read(fd, buf, *buflen);
	err_ret_if(*buflen < 0 || *buflen > maxlen, -1);

	i = 0;
	while (i < *buflen) {
		msgbuf[msglen] = buf[i];

		/* ebus syn sign is reached - decode ebus message */
		if (msgbuf[msglen] == EBUS_SYN) {
			if (rawdump)
				rawfile_write(msgbuf, msglen);

			debug_ebus_msg(msgbuf, msglen, nosyn);
			memset(msgbuf, '\0', sizeof(msgbuf));
			msglen = 0;
		} else {
			msglen++;
		}
		i++;
	}

	return 0;
}



void
cfg_print(struct config *cfg, int len)
{
	int i;

	for (i = 0; i < len; i++) {

		if (cfg[i].key != NULL && cfg[i].tgt != NULL) {
			fprintf(stdout, "%s = ", cfg[i].key);

			switch (cfg[i].type) {
			case STR:
				fprintf(stdout, "%s\n", (char *) cfg[i].tgt);
				break;
			case BOL:
				if (*(int *) cfg[i].tgt == NO)
					fprintf(stdout, "NO\n");
				else if (*(int *) cfg[i].tgt == YES)
					fprintf(stdout, "YES\n");
				else
					fprintf(stdout, "UNSET\n");
				break;
			case NUM:
				fprintf(stdout, "%d\n", *(int *) cfg[i].tgt);
				break;
			default:
				break;
			}
		}
	}
	fprintf(stdout, "\n");	
}

int
cfgfile_set_param(char *par, struct config *cfg, int len) {
	int i;

	for (i = 0; i < len; i++) {
		
		if (strncasecmp(par, cfg[i].key, strlen(cfg[i].key)) == 0 &&
		    strlen(par) == strlen(cfg[i].key)) {

			par = strtok(NULL, "\t =\n\r");

			switch (cfg[i].type) {
			case STR:
				if (strlen(cfg[i].tgt) == 0)
					strncpy(cfg[i].tgt , par, strlen(par));	
				break;
			case BOL:
				if (*(int *) cfg[i].tgt == UNSET) {
					if (strncasecmp(par, "NO", 2) == 0)
						*(int *) cfg[i].tgt = NO;
					else if (strncasecmp(par, "YES", 3) == 0)
						*(int *) cfg[i].tgt = YES;
					else
						*(int *) cfg[i].tgt = UNSET;
				}				
				break;
			case NUM:
				if (*(int *) cfg[i].tgt == UNSET)
					*(int *) cfg[i].tgt = atoi(par);	
				break;
			default:
				break;				
			}
		
			return 1;
		}
	}

	return 0;
}

int
cfgfile_read(const char *file, struct config *cfg, int len)
{
	int ret;
	char line[CFG_LINELEN];
	char *par;
	FILE *fp = NULL;

	/* open config file */
	fp = fopen(file, "r");
	err_ret_if(!fp, -1);

	/* read each line and set parameter */
	while (fgets( line, CFG_LINELEN, fp) != NULL ) {
		par = strtok(line, "\t =\n\r") ;
		
		if (par != NULL && par[0] != '#')
			cfgfile_set_param(par, cfg, len);
			
	}

	/* close config file */
	ret = fclose(fp);
	err_ret_if(ret == EOF, -1);

	return 0;
}


int
pidfile_open(const char *file, int *fd)
{
	int ret;
	char pid[10];

	*fd = open(file, O_RDWR|O_CREAT, 0600);
	err_ret_if(*fd < 0, -1);

	ret = lockf(*fd, F_TLOCK, 0);
	err_ret_if(ret < 0, -1);

	sprintf(pid, "%d\n", getpid());
	ret = write(*fd, pid, strlen(pid));
	err_ret_if(ret < 0, -1);

	return 0;
}

int
pidfile_close(const char *file, int fd)
{
	int ret;

	ret = close(fd);
	err_ret_if(ret < 0, -1);

	ret = unlink(file);
	err_ret_if(ret < 0, -1);

	return 0;
}



static FILE *rawfp = NULL;

int
rawfile_open(const char *file)
{
	rawfp = fopen(file, "w");
	err_ret_if(!rawfp, -1);

	return 0;
}

int
rawfile_close()
{
	int ret;

	ret = fflush(rawfp);
	err_ret_if(ret == EOF, -1);

	ret = fclose(rawfp);
	err_ret_if(ret == EOF, -1);

	return 0;
}

int
rawfile_write(unsigned char buf[], int buflen)
{
	int ret, i;

	for (i = 0; i <= buflen; i++) {
		ret = fputc(buf[i], rawfp);
		err_ret_if(ret == EOF, -1);
	}

	ret = fflush(rawfp);
	err_ret_if(ret == EOF, -1);

	return 0;
}



int
socket_open(int *fd, int port)
{
	int ret, opt;
	struct sockaddr_in sock;

	*fd = socket(PF_INET, SOCK_STREAM, 0);
	err_ret_if(fd < 0, -1);

	//todo: verify if this realy work
	/* prevent "Error Address already in use" error message */
	opt = 1;
	ret = setsockopt(*fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(int));
	err_ret_if(ret < 0, -1);

	memset((char *) &sock, 0, sizeof(sock));
	sock.sin_family = AF_INET;
	sock.sin_addr.s_addr = htonl(INADDR_ANY);
	sock.sin_port = htons(port);

	ret = bind(*fd, (struct sockaddr *) &sock, sizeof(sock));
	err_ret_if(ret < 0, -1);

	ret = listen(*fd, 5);
	err_ret_if(ret < 0, -1);

	return 0;
}

int
socket_close(int fd)
{
	int ret;

	ret = close(fd);
	err_ret_if(ret < 0, -1);

	return 0;
}

int
socket_client_accept(int listenfd, int *datafd)
{
	struct sockaddr_in sock;
	socklen_t socklen;

	socklen = sizeof(sock);

	*datafd = accept(listenfd, (struct sockaddr *) &sock, &socklen);
	err_ret_if(*datafd < 0, -1);

	return 0;
}

int
socket_client_read(int fd, char buf[], int *buflen)
{
	*buflen = read(fd, buf, *buflen);
	err_ret_if(*buflen < 0, -1);

	if (strncmp("quit", buf ,4) == 0) {
		/* close tcp connection */
		close(fd);
		return -1;
	}

	return 0;
}

int
socket_client_write(int fd, char buf[], int buflen)
{
	int ret;

	ret = write(fd, buf, buflen);
	err_ret_if(ret < 0, -1);

	return 0;
}
