import socket

def udp_server():
    # Define the server address and port
    server_address = ('', 7001)

    # Create a UDP socket
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        # Bind the socket to the server address
        sock.bind(server_address)
        print('Server is listening on port 7001...')

        while True:
            # Wait to receive a message
            print('Waiting to receive message...')
            data, address = sock.recvfrom(4096)

            print(f'Received {len(data)} bytes from {address}')
            print(f'Data: {data}')

            if data:
                # Send the received message back to the sender
                sent = sock.sendto(data, address)
                print(f'Sent {sent} bytes back to {address}')

if __name__ == '__main__':
    udp_server()

