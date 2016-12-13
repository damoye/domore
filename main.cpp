#include <fstream>
#include <sstream>
#include <thread>
#include <vector>
#include <iomanip>
#include "util.h"

using namespace std;
constexpr int CONCURRENCY = 4;

class Worker {
public:
    Worker(const Url &url, unsigned long start, unsigned long end, const string &part_filename) :
            url(url), start(start), end(end), part_filename(part_filename) {}

    void run() {
        t = thread(&Worker::download, this);
    }

    void join() {
        t.join();
    }

    double downloaded = 0;
    bool finished = false;
    unsigned long start;
    unsigned long end;
    string part_filename;

private:
    void download() {
        Socket socket(tcp_connect(url.host, "80"));
        ostringstream oss;
        oss << "GET " << url.path << " HTTP/1.1\r\n"
            << "Host: " << url.host << "\r\n"
            << "Connection: close\r\n"
            << "Range: " << "bytes=" << start << "-" << end << "\r\n\r\n";
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
        std::ofstream out(part_filename);
        while ((content = socket.read_more()) != "") {
            out << content;
            downloaded += content.size();
        }
        out.close();
        finished = true;
    }

    const Url &url;
    thread t;
};

void download(const string &url_s, const string &filename) {
    cout << "started: " << current_time() << endl;
    Url url(url_s);
    Socket socket(tcp_connect(url.host, "80"));
    ostringstream oss;
    oss << "GET " << url.path << " HTTP/1.1\r\n"
        << "Host: " << url.host << "\r\n"
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
    vector<Worker> workers;
    for (int i = 0; i < CONCURRENCY; ++i) {
        unsigned long start = i * average, end;
        if (i == CONCURRENCY - 1) {
            end = total;
        } else {
            end = start + average - 1;
        }
        workers.push_back(Worker(url, start, end, filename + ".domore" + to_string(i)));
    }
    for (Worker &worker : workers) {
        worker.run();
    }
    // for (Worker &worker : workers) {
    //     worker.join();
    //     ifstream part_file(worker.part_filename);
    //     file << part_file.rdbuf();
    // }
    cout << fixed << setprecision(2);
    while (true) {
        cout << "\r";
        bool all_finished = true;
        for (Worker &worker: workers) {
            cout << worker.downloaded / (worker.end - worker.start) * 100 << "% ";
            if (!worker.finished) {
                all_finished = false;
            }
        }
        cout << flush;
        if (all_finished) {
            break;
        }
        sleep(1);
    }
    ofstream file(filename);
    for (Worker &worker : workers) {
        ifstream part_file(worker.part_filename);
        file << part_file.rdbuf();
    }

    cout << "finished: " << current_time() << endl;
}

int main() {
    download("http://ftp.jaist.ac.jp/pub/qtproject/archive/qt/5.7/5.7.0/qt-opensource-mac-x64-clang-5.7.0.dmg",
             "/Users/mo/qt-opensource-mac-x64-clang-5.7.0.dmg");
}