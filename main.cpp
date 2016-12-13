#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>
#include <iomanip>
#include <netdb.h>
#include <unistd.h>

using namespace std;
constexpr int CONCURRENCY = 4;
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

class Url {
public:
    Url(const string &host, const string &path) : host(host), path(path) {}

    string host;
    string path;
};

class Mission {
public:
    Mission(unsigned long start, unsigned long end, const string &filename) :
            start(start), end(end), filename(filename) {}

    unsigned long start;
    unsigned long end;
    string filename;
    unsigned long downloaded = 0;
    bool finished = false;
};

shared_ptr<Url> parse_url(const string &url) {
    const string PROTOCOL_END("://");
    size_t prot_i = url.find(PROTOCOL_END);
    string protocol = url.substr(0, prot_i);
    if (protocol != "http" && protocol != "https") {
        throw MoException("only support http url");
    }
    prot_i += PROTOCOL_END.size();
    size_t path_i = url.find('/', prot_i);
    if (path_i == string::npos) {
        throw MoException("no path in url");
    }
    string host = url.substr(prot_i, path_i - prot_i);
    string path = url.substr(path_i);
    return make_shared<Url>(host, path);

}

string current_time() {
    time_t now = time(0);
    return string(ctime(&now));
}

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

void work(shared_ptr<Url> url, shared_ptr<Mission> mission) {
    Socket socket(tcp_connect(url->host, "80"));
    ostringstream oss;
    oss << "GET " << url->path << " HTTP/1.1\r\n"
        << "Host: " << url->host << "\r\n"
        << "Connection: close\r\n"
        << "Range: " << "bytes=" << mission->start << "-" << mission->end << "\r\n\r\n";
    socket.send_all(oss.str());
    while (true) {
        string line = socket.read_line();
        if (line == "\r\n") {
            break;
        }
        if (line == "") {
            throw MoException("content not found");
        }
    }
    string content;
    ofstream out(mission->filename);
    while ((content = socket.read_more()) != "") {
        out << content;
        mission->downloaded += content.size();
    }
    out.close();
    mission->finished = true;
}

void download(const string &url_s, const string &filename) {
    cout << "started: " << current_time() << flush;
    auto url = parse_url(url_s);
    Socket socket(tcp_connect(url->host, "80"));
    ostringstream oss;
    oss << "GET " << url->path << " HTTP/1.1\r\n"
        << "Host: " << url->host << "\r\n"
        << "Connection: close\r\n\r\n";
    socket.send_all(oss.str());
    string line;
    const string CONTENT_LENGTH("Content-Length: ");
    while (true) {
        line = socket.read_line();
        if (line.find(CONTENT_LENGTH) == 0) {
            break;
        }
        if (line == "" || line == "\r\n") {
            throw MoException("header content-length not found");
        }
    }
    unsigned long total = stoul(line.substr(CONTENT_LENGTH.size(), line.size() - 2 - CONTENT_LENGTH.size()));
    unsigned long average = total / CONCURRENCY;
    cout << "total: " << total << endl;
    vector<shared_ptr<Mission> > missions;
    vector<thread> workers;
    for (int i = 0; i < CONCURRENCY; ++i) {
        unsigned long start = i * average, end;
        if (i == CONCURRENCY - 1) {
            end = total;
        } else {
            end = start + average - 1;
        }
        auto mission = make_shared<Mission>(start, end, filename + ".domore" + to_string(i));
        workers.push_back(thread(work, url, mission));
        missions.push_back(mission);
    }
    cout << fixed << setprecision(2);
    while (true) {
        cout << "\r";
        bool all_finished = true;
        double downloaded = 0;
        for (auto mission: missions) {
            downloaded += mission->downloaded;
            if (!mission->finished) {
                all_finished = false;
            }
        }
        cout << downloaded * 10000 / total << "% ";
        cout << flush;
        if (all_finished) {
            break;
        }
        sleep(1);
    }
    ofstream file(filename);
    for (auto mission : missions) {
        ifstream part_file(mission->filename);
        file << part_file.rdbuf();
    }

    cout << "finished: " << current_time() << flush;
}

int main() {
    download("http://ftp.jaist.ac.jp/pub/qtproject/archive/qt/5.7/5.7.0/qt-opensource-mac-x64-clang-5.7.0.dmg",
             "/Users/mo/qt-opensource-mac-x64-clang-5.7.0.dmg");
}
