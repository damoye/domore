#ifndef DOMORE_UTIL_H
#define DOMORE_UTIL_H

#include <exception>
#include <iostream>
#include <string>
#include <netdb.h>
#include <unistd.h>

using std::string;
constexpr int BUFF_SIZE = 16384;

class MoException : public std::exception {
public:
    MoException(const string message) : _message(message) {}

private:
    virtual const char *what() const throw() {
        return _message.c_str();
    }

    const string _message;
};

class Socket {
public:
    Socket(int sockfd) : sockfd(sockfd) {}

    ~Socket() {
        close(sockfd);
    }

    void do_read() {
        if (read_count == 0) {
            while ((read_count = read(sockfd, read_buffer, sizeof(read_buffer))) < 0 && errno == EINTR) {}
            if (read_count < 0) {
                string error("read failed: ");
                error += strerror(errno);
                throw MoException(error);
            } else if (read_count > 0) {
                read_ptr = read_buffer;
            }
        }
    }

    string read_line() {
        string result;
        while (true) {
            do_read();
            if (read_count == 0) {
                return result;
            }
            read_count--;
            char c = *read_ptr++;
            result.push_back(c);
            if (c == '\n') {
                return result;
            }
        }
    }

    string read_more() {
        do_read();
        if (read_count == 0) {
            return "";
        }
        string result(read_ptr, read_count);
        read_count = 0;
        return result;
    }

    void send_all(const string &content) {
        const char *ptr = content.c_str();
        size_t left = content.size();
        while (left > 0) {
            ssize_t written = send(sockfd, ptr, left, 0);
            if (written <= 0) {
                if (written < 0 && errno == EINTR)
                    continue;
                else {
                    string error("send failed: ");
                    error += strerror(errno);
                    throw MoException(error);
                }
            }
            left -= written;
            ptr += written;
        }
    }

private:
    int sockfd;
    char read_buffer[BUFF_SIZE];
    char *read_ptr;
    ssize_t read_count = 0;
};

int tcp_connect(const string &host, const string &service) {
    addrinfo hints, *res, *res_save;
    bzero(&hints, sizeof(struct addrinfo));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    int n = getaddrinfo(host.c_str(), service.c_str(), &hints, &res);
    if (n != 0) {
        string error("getaddrinfo failed: ");
        error += gai_strerror(n);
        throw MoException(error);
    }
    int sockfd;
    res_save = res;
    while (true) {
        sockfd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sockfd >= 0) {
            if (connect(sockfd, res->ai_addr, res->ai_addrlen) == 0) {
                break;
            }
            close(sockfd);
        }
        if ((res = res->ai_next) == nullptr) {
            throw MoException("socket or connect all failed");
        }
    }
    freeaddrinfo(res_save);
    return sockfd;
}

class Url {
public:
    Url(const string &url) {
        const string PROTOCOL_END("://");
        size_t prot_i = url.find(PROTOCOL_END);
        protocol = url.substr(0, prot_i);
        prot_i += PROTOCOL_END.size();
        size_t path_i = url.find('/', prot_i);
        if (path_i == string::npos) {
            return;
        }
        host = url.substr(prot_i, path_i - prot_i);
        path = url.substr(path_i);
    }

    string protocol;
    string host;
    string path;
};

string current_time(){
    time_t now = time(0);
    return string(ctime(&now));
}

#endif //DOMORE_UTIL_H
