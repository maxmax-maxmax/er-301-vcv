import http.server
import socketserver
import os

os.chdir(".")

Handler = http.server.SimpleHTTPRequestHandler
with socketserver.TCPServer(("", 8090), Handler) as httpd:
    httpd.serve_forever()
