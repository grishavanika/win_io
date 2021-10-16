# From <https://gist.github.com/homoluctus/0a784df871fd0d0506173b03ccc67d9e>.
# -*- coding:utf-8 -*-

import socket
from datetime import datetime

# address and port is arbitrary
def server(host='127.0.0.1', port=60260):
  # create socket
  with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
    sock.bind((host, port))
    print("[+] Listening on {0}:{1}".format(host, port))
    sock.listen(5)
    # permit to access
    conn, addr = sock.accept()

    with conn as c:
      # display the current time
      time = datetime.now().ctime()
      print("[+] Connecting by {0}:{1} ({2})".format(addr[0], addr[1], time))

      while True:
        request = c.recv(4096)

        if not request:
          print("[-] Not Received")
          break

        print("[+] Received", repr(request.decode('utf-8')))

        #response = input("[+] Enter string : ")
        response = "RESPONSE"
        c.sendall(response.encode('utf-8'))
        print("[+] Sending to {0}:{1}".format(addr[0], addr[1]))

if __name__ == "__main__":
  server()
