/*
 * File: ReliableSocket.cpp
 *
 * Reliable data transport (RDT) library implementation.
 *
 * Author(s): Andrew Kirrane and Kevin McDonald
 *
 */

// C++ library includes
#include <iostream>
#include <string.h>

// OS specific includes
#include <unistd.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "ReliableSocket.h"
#include "rdt_time.h"

using std::cerr;

/*
 * NOTE: Function header comments shouldn't go in this file: they should be put
 * in the ReliableSocket header file.
 */

ReliableSocket::ReliableSocket() {
	this->sequence_number = 0;
	this->expected_sequence_number = 0;
	this->estimated_rtt = 100;
	this->dev_rtt = 10;
	this->current_rtt =0;

	// create new fields in your class, they should be
	// initialized here.

	this->sock_fd = socket(AF_INET, SOCK_DGRAM, 0);
	if (this->sock_fd < 0) {
		perror("socket");
		exit(EXIT_FAILURE);
	}

	this->state = INIT;
}

void ReliableSocket::accept_connection(int port_num) {
	if (this->state != INIT) {
		cerr << "Cannot call accept on used socket\n";
		exit(EXIT_FAILURE);
	}

	// Bind specified port num using our local IPv4 address.
	// This allows remote hosts to connect to a specific port.
	struct sockaddr_in addr; 
	addr.sin_family = AF_INET;
	addr.sin_port = htons(port_num);
	addr.sin_addr.s_addr = INADDR_ANY;
	if ( bind(this->sock_fd, (struct sockaddr*)&addr, sizeof(addr)) ) {
		perror("bind");
	}

	// Wait for a segment to come from a remote host
	char segment[MAX_SEG_SIZE];
	memset(segment, 0, MAX_SEG_SIZE);

	struct sockaddr_in fromaddr;
	unsigned int addrlen = sizeof(fromaddr);
	int recv_count = recvfrom(this->sock_fd, segment, MAX_SEG_SIZE, 0, (struct sockaddr*)&fromaddr, &addrlen);		
	if (recv_count < 0) {
		perror("accept recvfrom");
		exit(EXIT_FAILURE);
	}

	/*
	 * UDP isn't connection-oriented, but calling connect here allows us to
	 * remember the remote host (stored in fromaddr).
	 * This means we can then use send and recv instead of the more complex
	 * sendto and recvfrom.
	 */
	if (connect(this->sock_fd, (struct sockaddr*)&fromaddr, addrlen)) {
		perror("accept connect");
		exit(EXIT_FAILURE);
	}

	// Check that segment was the right type of message, namely a RDT_CONN
	// message to indicate that the remote host wants to start a new
	// connection with us.
	RDTHeader* hdr = (RDTHeader*)segment;
	cerr << hdr->type << "\n";
	cerr << RDT_SYN;
	if (hdr->type != RDT_SYN) {
		cerr << "ERROR: Didn't get the expected RDT_SYN type.\n";
		exit(EXIT_FAILURE);
	}

	//  implement a handshaking protocol to make sure that
	// both sides are correctly connected (e.g. what happens if the RDT_CONN
	// message from the other end gets lost?)
	// Note that this function is called by the connection receiver/listener.

	char sendSegment[MAX_SEG_SIZE]={0};
	char recvSegment[MAX_SEG_SIZE];

	hdr = (RDTHeader*) sendSegment;
	hdr->ack_number = htonl(0); //set ack number for initalizing handshake
	hdr->sequence_number = htonl(0); //set sequence number
	hdr->type = RDT_SYNACK;	

	while(1){
		set_timeout_length(this->estimated_rtt + (4* this->dev_rtt));
		this->send_seg_reliable(sendSegment, recvSegment, sizeof(RDTHeader));

		hdr = (RDTHeader*) recvSegment;

		if(hdr->type == RDT_ACK){
			break;
		}
		else if (hdr->type == RDT_DATA){
			break;
		}
		else{
			continue; //didn't reccieve ack or data
		}

	}

	this->state = ESTABLISHED;
	cerr << "INFO: Connection ESTABLISHED\n";
}

void ReliableSocket::connect_to_remote(char *hostname, int port_num) {
	if (this->state != INIT) {
		cerr << "Cannot call connect_to_remote on used socket\n";
		return;
	}

	// set up IPv4 address info with given hostname and port number
	struct sockaddr_in addr; 
	addr.sin_family = AF_INET; 	// use IPv4
	addr.sin_addr.s_addr = inet_addr(hostname);
	addr.sin_port = htons(port_num); 

	/*
	 * UDP isn't connection-oriented, but calling connect here allows us to
	 * remember the remote host (stored in fromaddr).
	 * This means we can then use send and recv instead of the more complex
	 * sendto and recvfrom.
	 */
	if(connect(this->sock_fd, (struct sockaddr*)&addr, sizeof(addr))) {
		perror("connect");
	}


	//implement a handshaking protocol for the
	// connection setup.
	// Note that this function is called by the connection initiator.

	char sendSegment[MAX_SEG_SIZE]={0};
	char recvSegment[MAX_SEG_SIZE];

	RDTHeader* hdr = (RDTHeader*)sendSegment;
	hdr->ack_number = htonl(0); //set ack number for initalizing handshake
	hdr->sequence_number = htonl(0); //set sequence number
	hdr->type = RDT_SYN;	

	this->set_timeout_length(this->estimated_rtt + (4* this->dev_rtt));
	this->send_seg_reliable(sendSegment, recvSegment, sizeof(RDTHeader));
	//clear hdr
	memset(hdr,0,sizeof(RDTHeader));
	hdr = (RDTHeader*)recvSegment;
	if (hdr->type != RDT_SYNACK) {
		perror("Not a SYNACK");
	}

	memset(sendSegment,0,sizeof(RDTHeader));
	hdr = (RDTHeader*)sendSegment;
	hdr->ack_number = htonl(0);
	hdr->sequence_number = htonl(0);
	hdr->type = RDT_ACK;
	this->send_timeout(sendSegment);


	this->state = ESTABLISHED;
	cerr << "INFO: Connection ESTABLISHED\n";
}


// You should not modify this function in any way.
uint32_t ReliableSocket::get_estimated_rtt() {
	return this->estimated_rtt;
}

void ReliableSocket::set_estimated_rtt() {
	//estimated rtt and dev rtt equation for lecture slides
	this->estimated_rtt *= (1 - 0.125);
	this->estimated_rtt += this->current_rtt * 0.125;
	this->dev_rtt = this->dev_rtt * (1-.25);
	
	int deviation = this->current_rtt - this->estimated_rtt;
	if(deviation < 0){
		deviation = deviation * -1;
	}	
	this->dev_rtt = dev_rtt + (deviation *.25);

	this->set_timeout_length(this->estimated_rtt + (4 * this->dev_rtt)); 
}

// You shouldn't need to modify this function in any way.
void ReliableSocket::set_timeout_length(uint32_t timeout_length_ms) {
	cerr << "INFO: Setting timeout to " << timeout_length_ms << " ms\n";
	struct timeval timeout;
	msec_to_timeval(timeout_length_ms, &timeout);

	if (setsockopt(this->sock_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout,
				sizeof(struct timeval)) < 0) {
		perror("setsockopt");
		exit(EXIT_FAILURE);
	}
}

void ReliableSocket::send_data(const void *data, int length) {
	if (this->state != ESTABLISHED) {
		cerr << "INFO: Cannot send: Connection not established.\n";
		return;
	}

	// Create the segment, which contains a header followed by the data.
	char sendSegment[MAX_SEG_SIZE]={0};
	char recvSegment[MAX_SEG_SIZE];

	// Fill in the header
	RDTHeader *hdr = (RDTHeader*)sendSegment;
	hdr->sequence_number = htonl(this->sequence_number);
	hdr->ack_number = htonl(0);
	hdr->type = RDT_DATA;

	// Copy the user-supplied data to the spot right past the 
	// 	header (i.e. hdr+1).
	memcpy(hdr+1, data, length);


	// waits for an acknowledgment of the data you just sent, and keeps
	// resending until that ack comes.
	// Utilize the set_timeout_length function to make sure you timeout after
	// a certain amount of waiting (so you can try sending again).

	while(1) {
		memset(recvSegment,0,MAX_SEG_SIZE);
		send_seg_reliable(sendSegment, recvSegment, sizeof(RDTHeader) + length);

		hdr = (RDTHeader*)recvSegment;
		if (hdr->type == RDT_ACK) {
			if (this->sequence_number == ntohl(hdr->ack_number)) {
				break; // Recieved desired ACK
			}
			else {
				continue; // Out of order ACK
			}	
		}
		else{
			continue; // No ACK so loop again
		} 
	}
	sequence_number++;
}


int ReliableSocket::receive_data(char buffer[MAX_DATA_SIZE]) {
	if (this->state != ESTABLISHED) {
		cerr << "INFO: Cannot receive: Connection not established.\n";
		return 0;
	}
	int recv_data_size = 0;
	this->set_timeout_length(0);
	while(1) {
		char sendSegment[sizeof(RDTHeader)]={0};
		char recvSegment[MAX_SEG_SIZE];
		memset(recvSegment,0,MAX_SEG_SIZE);

		// Set up pointers to both the header (hdr) and data (data) portions of
		// the received segment.
		RDTHeader* hdr = (RDTHeader*)recvSegment;	
		void *data = (void*)(recvSegment + sizeof(RDTHeader));

		int recv_count = recv(this->sock_fd, recvSegment, MAX_SEG_SIZE, 0);
		if (recv_count < 0) {
			perror("receive_data recv");
			exit(EXIT_FAILURE);
		}

		// acknowledment that you
		// received some data, but first you'll need to make sure that what you
		// received is the type you want (RDT_DATA) and has the right sequence
		// number.

		cerr << "INFO: Received segment. " 
			<< "seq_num = "<< ntohl(hdr->sequence_number) << ", "
			<< "ack_num = "<< ntohl(hdr->ack_number) << ", "
			<< ", type = " << hdr->type << "\n";
		
		uint32_t sequence_num = hdr->sequence_number;

		if (hdr->type == RDT_ACK) {
			//let the ack timeout for the sender for the inital 3 way
			//handshake
			continue;	
		}
		if (hdr->type == RDT_CLOSE) {
			hdr = (RDTHeader*)sendSegment;
			hdr->sequence_number = htonl(0);
			hdr->ack_number = htonl(0);
			hdr->type = RDT_ACK;
			
			this->send_timeout(sendSegment);
			
			this->state = FIN;
			break;	
		}
		else {
			// ACK recieved packet
			hdr = (RDTHeader*)sendSegment;
			hdr->sequence_number = sequence_num;
			hdr->ack_number = sequence_num;
			hdr->type = RDT_ACK;
			if (send(this->sock_fd, sendSegment, sizeof(RDTHeader), 0) < 0) {
				perror("receive_data send error");	
			}
			if (ntohl(sequence_num) == this->sequence_number) {
				// Got desired packet, end loop	
			}
			else {
				continue; // Out of order sequence number, drop packet	
			}
		}
	this->sequence_number += 1;	
	recv_data_size = recv_count - sizeof(RDTHeader);
	memcpy(buffer, data, recv_data_size);
	break;
	}

	return recv_data_size;
}


void ReliableSocket::close_connection() {
	// Construct a RDT_CLOSE message to indicate to the remote host that we
	// want to end this connection.
	if(this->state != FIN){
		this->send_close();
	}
	else {
		this->recv_close();	
	}
	this->state = CLOSED;

	if (close(this->sock_fd) < 0) {
		perror("close_connection close");
	}
	cerr << "Connection Closed as Expected\n";
}

void ReliableSocket::send_close() {
	char sendSegment[MAX_SEG_SIZE]={0};
	char recvSegment[MAX_SEG_SIZE];
	
	RDTHeader *hdr = (RDTHeader*)sendSegment;
	hdr->sequence_number = htonl(0);
	hdr->ack_number = htonl(0);
	hdr->type = RDT_CLOSE;
	
	while(1) {
		//itilize close message
		this->send_seg_reliable(sendSegment, recvSegment, sizeof(RDTHeader));
		hdr = (RDTHeader*)recvSegment;
		if (hdr->type == RDT_ACK) {
			break;	
		}
		//check if the ack was dropped if that is the case then the server is
		//at next step in the teardown process
		if (hdr->type == RDT_CLOSE) {
			break;	
		}
		memset(recvSegment,0,MAX_SEG_SIZE);
	}
	
	while(1) {
		memset(recvSegment, 0, MAX_SEG_SIZE);
		int received_bytes = recv(this->sock_fd, recvSegment, MAX_SEG_SIZE, 0);
		if (received_bytes < 0 && errno != EAGAIN) {
			perror("send_close recv error");
			exit(EXIT_FAILURE);	
		}
		else if (received_bytes < 0) {
			continue;  //timeout	
		} 
		
		hdr = (RDTHeader*)recvSegment;
		if (hdr->type == RDT_CLOSE) {
			//we got the close
			break;	
		}
	}
	
	hdr = (RDTHeader*)sendSegment;
	hdr->type = RDT_ACK;
	
	while(1){
		if (send(this->sock_fd, sendSegment, sizeof(RDTHeader), 0) < 0) {
			perror("send_close send error");		
		}
		
		memset(recvSegment, 0, MAX_SEG_SIZE);
		this->set_timeout_length(WAIT_TIME);
		if (recv(this->sock_fd, recvSegment, MAX_SEG_SIZE, 0) > 0) {
			hdr = (RDTHeader*)recvSegment;
			if (hdr->type == RDT_CLOSE) {
				continue;
				//lost the ack try again	
			}	
		}
		else {
			if (errno == EAGAIN) {
				break;
				//got the timeout so we can close	
			}
			else {
				perror("send_close recv error");
				exit(EXIT_FAILURE);	
			}	
		}	
	}	
}


void ReliableSocket::recv_close() {
	char sendSegment[MAX_SEG_SIZE]={0};
	char recvSegment[MAX_SEG_SIZE];
	
	RDTHeader *hdr = (RDTHeader*)sendSegment;
	hdr->sequence_number = htonl(0);
	hdr->ack_number = htonl(0);
	hdr->type = RDT_CLOSE;
	
	set_timeout_length(this->estimated_rtt + (4* this->dev_rtt));
	//if timeout happens then we need to resend the close
	while(1) {
		//keep sending close until we get the final ack 
		memset(recvSegment,0,MAX_SEG_SIZE);
		this->send_seg_reliable(sendSegment, recvSegment, sizeof(RDTHeader));
		hdr = (RDTHeader*)recvSegment;
		if (hdr->type == RDT_ACK) {
			break;	
		}
	}
}

void ReliableSocket::send_seg_reliable(char sendSegment[MAX_SEG_SIZE], char recvSegment[MAX_SEG_SIZE], int senderSize){
	int airTime; 
	this->set_timeout_length(this->estimated_rtt + (4*this->dev_rtt));
	bool lastTimeout = false; //bool did we timeout last time or not
	uint32_t curTimeout; //stores previous timeout length

	while(1){
		airTime = current_msec(); //get current time 
		if(send(this->sock_fd, sendSegment, senderSize, 0) < 0){ perror("reliable send failed");}
		//clear recv buffer to be ready to write new info in
		memset(recvSegment,0,MAX_SEG_SIZE);
		int numBytes = recv(this->sock_fd, recvSegment, MAX_SEG_SIZE,0);
		if(numBytes < 0){
			if(errno == EAGAIN){
				cerr << "TIMEOUT. DOUBLING THE LENGTH OF TIMEOUT\n";
				if(lastTimeout){
					curTimeout = 2*curTimeout; //double timeout length
				}
				else{
					curTimeout = (this->estimated_rtt + (4*this->dev_rtt))*2; //set timeout length
				}
				this->set_timeout_length(curTimeout);
				lastTimeout = true;
				continue;
			}
			else{
				perror("No ACK recv");
				exit(EXIT_FAILURE);
			}

		}
		this->current_rtt = current_msec() - airTime; //the time it took to send
		break;
	}
	this->set_estimated_rtt();

}

void ReliableSocket::send_timeout(char sendSegment[MAX_SEG_SIZE]) {
	char recvSegment[MAX_SEG_SIZE];

	while(1) {
		if (send(this->sock_fd, sendSegment, sizeof(RDTHeader), 0) < 0) {
			perror("send_timeout send error");	
		}

		memset(recvSegment,0,MAX_SEG_SIZE);
		set_timeout_length(this->estimated_rtt + (4* this->dev_rtt));
		if (recv(this->sock_fd, recvSegment, MAX_SEG_SIZE, 0) < 0) {
			if (errno == EAGAIN) {
				break; // timeout
			}
			else {
				perror("send_timeout recieve error");	
				exit(EXIT_FAILURE);
			}	
		}
		else {
			continue; // received packet so loop again
		}	
	}	
}
