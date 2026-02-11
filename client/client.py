import socket
import datetime
import socket as s

SERVER_IP = "127.0.0.1"
SERVER_PORT = 5555

hostname = s.gethostname()
local_ip = s.gethostbyname(hostname)

sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
sock.connect((SERVER_IP, SERVER_PORT))

print("Connected to server")

while True:
    data = input("Enter text: ")

    timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    message = f"{timestamp}|{local_ip}|{hostname}|{data}\n"

    sock.sendall(message.encode())

