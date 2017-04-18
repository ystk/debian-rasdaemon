#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "ras-report.h"

static int setup_report_socket(void){
	int sockfd = -1;
	int rc = -1;
	struct sockaddr_un addr;

	sockfd = socket(AF_UNIX, SOCK_STREAM, 0);
	if (sockfd < 0){
		return -1;
	}

	memset(&addr, 0, sizeof(struct sockaddr_un));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, ABRT_SOCKET, strlen(ABRT_SOCKET));

	rc = connect(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_un));
	if (rc < 0){
		return -1;
	}

	return sockfd;
}

static int commit_report_basic(int sockfd){
	char buf[INPUT_BUFFER_SIZE];
	struct utsname un;
	int rc = -1;

	if(sockfd < 0){
		return rc;
	}

	memset(buf, 0, INPUT_BUFFER_SIZE);
	memset(&un, 0, sizeof(struct utsname));

	rc = uname(&un);
	if(rc < 0){
		return rc;
	}

	/*
	 * ABRT server protocol
	 */
	sprintf(buf, "PUT / HTTP/1.1\r\n\r\n");
	rc = write(sockfd, buf, strlen(buf));
	if(rc < strlen(buf)){
		return -1;
	}

	sprintf(buf, "PID=%d", (int)getpid());
	rc = write(sockfd, buf, strlen(buf) + 1);
	if(rc < strlen(buf) + 1){
		return -1;
	}

	sprintf(buf, "EXECUTABLE=/boot/vmlinuz-%s", un.release);
	rc = write(sockfd, buf, strlen(buf) + 1);
	if(rc < strlen(buf) + 1){
		return -1;
	}

	sprintf(buf, "TYPE=%s", "ras");
	rc = write(sockfd, buf, strlen(buf) + 1);
	if(rc < strlen(buf) + 1){
		return -1;
	}

	return 0;
}

static int set_mc_event_backtrace(char *buf, struct ras_mc_event *ev){
	char bt_buf[MAX_BACKTRACE_SIZE];

	if(!buf || !ev)
		return -1;

	sprintf(bt_buf, "BACKTRACE="	\
						"timestamp=%s\n"	\
						"error_count=%d\n"	\
						"error_type=%s\n"	\
						"msg=%s\n"	\
						"label=%s\n"	\
						"mc_index=%c\n"	\
						"top_layer=%c\n"	\
						"middle_layer=%c\n"	\
						"lower_layer=%c\n"	\
						"address=%llu\n"	\
						"grain=%llu\n"	\
						"syndrome=%llu\n"	\
						"driver_detail=%s\n",	\
						ev->timestamp,	\
						ev->error_count,	\
						ev->error_type,	\
						ev->msg,	\
						ev->label,	\
						ev->mc_index,	\
						ev->top_layer,	\
						ev->middle_layer,	\
						ev->lower_layer,	\
						ev->address,	\
						ev->grain,	\
						ev->syndrome,	\
						ev->driver_detail);

	strcat(buf, bt_buf);

	return 0;
}

static int set_mce_event_backtrace(char *buf, struct mce_event *ev){
	char bt_buf[MAX_BACKTRACE_SIZE];

	if(!buf || !ev)
		return -1;

	sprintf(bt_buf, "BACKTRACE="	\
						"timestamp=%s\n"	\
						"bank_name=%s\n"	\
						"error_msg=%s\n"	\
						"mcgstatus_msg=%s\n"	\
						"mcistatus_msg=%s\n"	\
						"mcastatus_msg=%s\n"	\
						"user_action=%s\n"	\
						"mc_location=%s\n"	\
						"mcgcap=%lu\n"	\
						"mcgstatus=%lu\n"	\
						"status=%lu\n"	\
						"addr=%lu\n"	\
						"misc=%lu\n"	\
						"ip=%lu\n"	\
						"tsc=%lu\n"	\
						"walltime=%lu\n"	\
						"cpu=%u\n"	\
						"cpuid=%u\n"	\
						"apicid=%u\n"	\
						"socketid=%u\n"	\
						"cs=%d\n"	\
						"bank=%d\n"	\
						"cpuvendor=%d\n",	\
						ev->timestamp,	\
						ev->bank_name,	\
						ev->error_msg,	\
						ev->mcgstatus_msg,	\
						ev->mcistatus_msg,	\
						ev->mcastatus_msg,	\
						ev->user_action,	\
						ev->mc_location,	\
						ev->mcgcap,	\
						ev->mcgstatus,	\
						ev->status,	\
						ev->addr,	\
						ev->misc,	\
						ev->ip,	\
						ev->tsc,	\
						ev->walltime,	\
						ev->cpu,	\
						ev->cpuid,	\
						ev->apicid,	\
						ev->socketid,	\
						ev->cs,	\
						ev->bank,	\
						ev->cpuvendor);

	strcat(buf, bt_buf);

	return 0;
}

static int set_aer_event_backtrace(char *buf, struct ras_aer_event *ev){
	char bt_buf[MAX_BACKTRACE_SIZE];

	if(!buf || !ev)
		return -1;

	sprintf(bt_buf, "BACKTRACE="	\
						"timestamp=%s\n"	\
						"error_type=%s\n"	\
						"dev_name=%s\n"	\
						"msg=%s\n",	\
						ev->timestamp,	\
						ev->error_type,	\
						ev->dev_name,	\
						ev->msg);

	strcat(buf, bt_buf);

	return 0;
}

static int commit_report_backtrace(int sockfd, int type, void *ev){
	char buf[MAX_BACKTRACE_SIZE];
	char *pbuf = buf;
	int rc = -1;
	int buf_len = 0;

	if(sockfd < 0 || !ev){
		return -1;
	}

	memset(buf, 0, MAX_BACKTRACE_SIZE);

	switch(type){
	case MC_EVENT:
		rc = set_mc_event_backtrace(buf, (struct ras_mc_event *)ev);
		break;
	case AER_EVENT:
		rc = set_aer_event_backtrace(buf, (struct ras_aer_event *)ev);
		break;
	case MCE_EVENT:
		rc = set_mce_event_backtrace(buf, (struct mce_event *)ev);
		break;
	default:
		return -1;
	}

	if(rc < 0){
		return -1;
	}

	buf_len = strlen(buf);

	for(;buf_len > INPUT_BUFFER_SIZE - 1; buf_len -= (INPUT_BUFFER_SIZE - 1)){
		rc = write(sockfd, pbuf, INPUT_BUFFER_SIZE - 1);
		if(rc < INPUT_BUFFER_SIZE - 1){
			return -1;
		}

		pbuf = pbuf + INPUT_BUFFER_SIZE - 1;
	}

	rc = write(sockfd, pbuf, buf_len + 1);
	if(rc < buf_len){
		return -1;
	}

	return 0;
}

int ras_report_mc_event(struct ras_events *ras, struct ras_mc_event *ev){
	char buf[MAX_MESSAGE_SIZE];
	int sockfd = -1;
	int done = 0;
	int rc = -1;

	memset(buf, 0, sizeof(buf));

	sockfd = setup_report_socket();
	if(sockfd < 0){
		return -1;
	}

	rc = commit_report_basic(sockfd);
	if(rc < 0){
		goto mc_fail;
	}

	rc = commit_report_backtrace(sockfd, MC_EVENT, ev);
	if(rc < 0){
		goto mc_fail;
	}

	sprintf(buf, "ANALYZER=%s", "rasdaemon-mc");
	rc = write(sockfd, buf, strlen(buf) + 1);
	if(rc < strlen(buf) + 1){
		goto mc_fail;
	}

	sprintf(buf, "REASON=%s", "EDAC driver report problem");
	rc = write(sockfd, buf, strlen(buf) + 1);
	if(rc < strlen(buf) + 1){
		goto mc_fail;
	}

	done = 1;

mc_fail:

	if(sockfd > 0){
		close(sockfd);
	}

	if(done){
		return 0;
	}else{
		return -1;
	}
}

int ras_report_aer_event(struct ras_events *ras, struct ras_aer_event *ev){
	char buf[MAX_MESSAGE_SIZE];
	int sockfd = 0;
	int done = 0;
	int rc = -1;

	memset(buf, 0, sizeof(buf));

	sockfd = setup_report_socket();
	if(sockfd < 0){
		return -1;
	}

	rc = commit_report_basic(sockfd);
	if(rc < 0){
		goto aer_fail;
	}

	rc = commit_report_backtrace(sockfd, AER_EVENT, ev);
	if(rc < 0){
		goto aer_fail;
	}

	sprintf(buf, "ANALYZER=%s", "rasdaemon-aer");
	rc = write(sockfd, buf, strlen(buf) + 1);
	if(rc < strlen(buf) + 1){
		goto aer_fail;
	}

	sprintf(buf, "REASON=%s", "PCIe AER driver report problem");
	rc = write(sockfd, buf, strlen(buf) + 1);
	if(rc < strlen(buf) + 1){
		goto aer_fail;
	}

	done = 1;

aer_fail:

	if(sockfd > 0){
		close(sockfd);
	}

	if(done){
		return 0;
	}else{
		return -1;
	}
}

int ras_report_mce_event(struct ras_events *ras, struct mce_event *ev){
	char buf[MAX_MESSAGE_SIZE];
	int sockfd = 0;
	int done = 0;
	int rc = -1;

	memset(buf, 0, sizeof(buf));

	sockfd = setup_report_socket();
	if(sockfd < 0){
		return -1;
	}

	rc = commit_report_basic(sockfd);
	if(rc < 0){
		goto mce_fail;
	}

	rc = commit_report_backtrace(sockfd, MCE_EVENT, ev);
	if(rc < 0){
		goto mce_fail;
	}

	sprintf(buf, "ANALYZER=%s", "rasdaemon-mce");
	rc = write(sockfd, buf, strlen(buf) + 1);
	if(rc < strlen(buf) + 1){
		goto mce_fail;
	}

	sprintf(buf, "REASON=%s", "Machine Check driver report problem");
	rc = write(sockfd, buf, strlen(buf) + 1);
	if(rc < strlen(buf) + 1){
		goto mce_fail;
	}

	done = 1;

mce_fail:

	if(sockfd > 0){
		close(sockfd);
	}

	if(done){
		return 0;
	}else{
		return -1;
	}
}
