import socket
import struct

def send_req(sock, args):
    # Sérialisation au format TLV / longueur-préfixée
    body = struct.pack("<I", len(args)) # Nombre d'arguments (nstr)
    for arg in args:
        arg_bytes = arg.encode("utf-8")
        body += struct.pack("<I", len(arg_bytes)) + arg_bytes
    
    header = struct.pack("<I", len(body))
    sock.sendall(header + body)

def read_res(sock):
    header = sock.recv(4)
    if not header:
        return None
    length = struct.unpack("<I", header)[0]
    data = sock.recv(length)
    
    tag = data[0]
    if tag == 0: 
        return "NIL (Succès / Vide)"
    elif tag == 1: 
        msg_len = struct.unpack("<I", data[1:5])[0]
        return f"ERROR: {data[5:5+msg_len].decode('utf-8')}"
    elif tag == 2: 
        str_len = struct.unpack("<I", data[1:5])[0]
        return data[5:5+str_len].decode('utf-8')
    elif tag == 3: 
        return f"(int) {struct.unpack('<q', data[1:9])[0]}"
    elif tag == 4: 
        return f"(double) {struct.unpack('<d', data[1:9])[0]}"
    return data

def main():
    host = "127.0.0.1"
    port = 1234
    
    print("--- 🔌 Client Test Binaire Redis-like ---")
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.connect((host, port))
        
        # 1. Insertion (SET nom alice)
        print("Envoi : SET nom alice")
        send_req(s, ["set", "nom", "alice"])
        print(f"Réponse -> {read_res(s)}\n")
        
        # 2. Récupération (GET nom)
        print("Envoi : GET nom")
        send_req(s, ["get", "nom"])
        print(f"Réponse -> {read_res(s)}\n")
        
        # 3. Suppression (DEL nom)
        print("Envoi : DEL nom")
        send_req(s, ["del", "nom"])
        print(f"Réponse -> {read_res(s)}\n")

if __name__ == "__main__":
    main()

