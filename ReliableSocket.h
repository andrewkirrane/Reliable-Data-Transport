/*
 * File: ReliableSocket.h
 *
 * Header / API file for library that provides reliable data transport over an
 * unreliable link.
 *
 */

// TODO: You'll likely need to add some new types, as you start doing things
// like updating accept_connection and close_connection.
enum RDTMessageType : uint8_t {RDT_SYN, RDT_SYNACK,  RDT_ACK, RDT_DATA, RDT_CLOSE};

/**
 * Format for the header of a segment send by our reliable socket.
 */
struct RDTHeader {
	uint32_t sequence_number;
	uint32_t ack_number;
	RDTMessageType type;
};

// TODO: Again, you'll likely need to add new statuses (is that a word?) as
// you start implementing the reliable protocol.
enum connection_status { INIT, ESTABLISHED, FIN, CLOSED };

/**
 * Class that represents a socket using a reliable data transport protocol.
 * This socket uses a stop-and-wait protocol so your data is sent at a nice,
 * leisurely pace.
 */
class ReliableSocket {
public:
	/*
	 * You probably shouldn't add any more public members to this class.
	 * Any new functions or fields you need to add should be private.
	 */
	
	// These are constants for all reliable connections
	static const int MAX_SEG_SIZE  = 1400;
	static const int WAIT_TIME = 4000;
	static const int MAX_DATA_SIZE = MAX_SEG_SIZE - sizeof(RDTHeader);

	/**
	 * Basic Constructor, setting estimated RTT to 100 and deviation RTT to 10.
	 */
	ReliableSocket();

	/**
	 * Connects to the specified remote hostname on the given port.
	 *
	 * @param hostname Name of the remote host to connect to.
	 * @param port_num Port number of remote host.
	 */
	void connect_to_remote(char *hostname, int port_num);

	/**
	 * Waits for a connection attempt from a remote host.
	 *
	 * @param port_num The port number to listen on.
	 */
	void accept_connection(int port_num);

	/**
	 * Send data to connected remote host.
	 *
	 * @param buffer The buffer with data to be sent.
	 * @param length The amount of data in the buffer to send.
	 */
	void send_data(const void *buffer, int length);

	/**
	 * Receives data from remote host using a reliable connection.
	 *
	 * @param buffer The buffer where received data will be stored.
	 * @return The amount of data actually received.
	 */
	int receive_data(char buffer[MAX_DATA_SIZE]);

	/**
	 * Closes an connection.
	 */
	void close_connection();

	/**
	 * Returns the estimated RTT.
	 * 
	 * @return Estimated RTT for connection (in milliseconds)
	 */
	uint32_t get_estimated_rtt();

private:
	// Private member variables are initialized in the constructor
	int sock_fd;
	uint32_t sequence_number;
	uint32_t expected_sequence_number;
	int estimated_rtt;
	int dev_rtt;
	int current_rtt;
	connection_status state;

	// In the (unlikely?) event you need a new field, add it here.

	/**
	 * Sets the timeout length of this connection.
	 *
	 * @note Setting this to 0 makes the timeout length indefinite (i.e. could
	 * wait forever for a message).
	 *
	 * @param timeout_length_ms Length of timeout period in milliseconds.
	 */
	void set_timeout_length(uint32_t timeout_length_ms);

	/*
	 * Add new member functions (i.e. methods) after this point.
	 * Remember that only the comment and header line goes here. The
	 * implementation should be in the .cpp file.
	 */

	
	//This function calulates the length of the timeout using the equations we
	//learned in lecture. Once a new timeout is calculated using the
	//set_timeout_length we can adjust the timeout to match the newly
	//calcuated timeout
	void set_estimated_rtt();

 	//the senders portion of closing a connection
	void send_close();	

	//the recievers portion of closing a connection
	void recv_close();
	
	//First calls the send function then waits for a response. Upon the
	//response we store the response in a buffer. Finally we update the timeout
	//
	//@param sendSegement An array that stores the message we want to send
	//@param recvSegement An array that stores the message we recieved
	//@param senderSize the length of sendSegment
	void send_seg_reliable(char sendSegment[MAX_SEG_SIZE] ,char recvSegment[MAX_SEG_SIZE] , int senderSize);

	//first calls send then we want a timeout. if we do not timeout then send
	//again
	//
	//@param sendSegment An array that stores a message we want to send

	void send_timeout(char sendSegment[MAX_SEG_SIZE]);


};
