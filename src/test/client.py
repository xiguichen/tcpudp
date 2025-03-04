import socket

def udp_client():
    # Define the server address and port
    server_address = ('localhost', 5002)

    # Create a UDP socket
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        try:
            # Send data
            message = b'hello'
            print(f'Sending: {message}')
            sent = sock.sendto(message, server_address)

            # Receive response
            print('Waiting to receive...')
            data, server = sock.recvfrom(4096)
            print(f'Received: {data}')

            message = b'hello2'
            print(f'Sending: {message}')
            sent = sock.sendto(message, server_address)

            # Receive response
            print('Waiting to receive...')
            data, server = sock.recvfrom(4096)
            print(f'Received: {data}')

            # Send a longer message with 4096 * 4096 bytes
            message = b'x' * 4096;
            print(f'sending: {len(message)} bytes')
            sent = sock.sendto(message, server_address)

            # Receive response
            print('Waiting to receive...')
            data, server = sock.recvfrom(4096)
            # only print the received message length
            print(f'Received: {len(data)} bytes')

        except Exception as e:
            print(f'An error occurred: {e}')

if __name__ == '__main__':
    udp_client()

