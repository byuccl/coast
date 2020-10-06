import sys
import socket
import struct
import subprocess as sp
from threading import current_thread

import resources.utils as utils


# Functions for sending large amounts of data over a socket
# TODO: use htonl and ntohl for all network transfers, instead of relying on < or > in struct
# https://stackoverflow.com/a/17668009
def send_msg(sock, msg, encoding='utf-8'):
    """Send a message over a socket.

    This calculates the message length and adds a length header.
    Also makes sure the data has been encoded somehow.
    """

    # make sure it's bytes type
    if isinstance(msg, str):
        msg = msg.encode(encoding)

    # pack and send
    msg = struct.pack('>I', len(msg)) + msg
    sock.sendall(msg)


def recv_msg(sock, silent=False):
    """Receive data from a socket, prefixed with a length header.

    This does not decode the data in anyway, so it can be used
     the same way as the normal recv() function.
    """

    raw_msglen = recvall(sock, 4, silent=silent)
    if not raw_msglen:
        if not silent:
            print(utils.getFormattedTime())
            print("recv_msg(): didn't get a message length", file=sys.stderr)
        # return None
        raise ConnectionError
    msglen = struct.unpack('>I', raw_msglen)[0]
    return recvall(sock, msglen, silent=silent)


def recvall(sock, n, silent=False):
    """Helper function for recv_msg()."""
    data = b''
    while len(data) < n:
        try:
            packet = sock.recv(n - len(data))
            if not packet:
                return None
            data += packet
        except (socket.error, OSError) as e:
            if not silent:
                print("recvall() - Socket error:\n\t" + str(e), file=sys.stderr)
                print(current_thread().name, file=sys.stderr)
            raise ConnectionError
    return data


def getTcpPorts():
    """Returns a list of all of the TCP port addresses that are currently listening."""
    netstatProc = sp.Popen(['netstat', '-tlpn', 'LISTEN'], stdout=sp.PIPE, stderr=sp.DEVNULL)
    lines = [l for l in iter(netstatProc.stdout.readline, b'')]
    return [l.decode('utf-8').split()[3] for l in lines[2:]]


def getNextPort(ports, num):
    """Given a list of open ports, get the next open one from a starting location."""
    while True:
        if any(str(num) in p for p in ports):
            num += 1
        else:
            return num
