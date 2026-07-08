/**
 * @file pqutil.cpp
 * @brief Modernized interactive program for operating on an LDM product-queue.
 * @author Robert Toomey
 * @date May 2026
 */
#include "config.h"
#include "Application.h"
#include "Log.h"
#include "Pattern.h"
#include "Registry.h"
#include "NetworkUtils.h"
#include "IProductStore.h"
#include "StorageFactory.h"
#include "PrivilegeManager.h"
#include "Signature.h"
#include "FeedType.h"
#include "Timestamp.h"
#include "NetworkFactory.h"

#include <iostream>
#include <vector>
#include <string>
#include <sstream>
#include <unordered_map>
#include <fstream>
#include <cctype>
#include <cerrno>
#include <ctime>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <termios.h>
#include <unistd.h>
#include <sys/time.h>
#include <spdlog/fmt/fmt.h>

constexpr size_t DBUFMAX = 16384;

using namespace rdm;

enum class Cmd {
    SET, SHOW, READ, NEW, PUT, WRITE,
    DISCARD, DISPLAY, WATCH, DELETE, STATS,
    HELP, QUIT, COMMENT, BAD
};

enum class Opt {
    FEEDTYPE, PATTERN, CURSOR, CURSDIR, TOTIME,
    FROMTIME, ARRIVAL, PRODFTYPE, SEQNO, ORIGIN,
    IDENT, NONE, BAD
};

class PqUtilApp : public Application {
private:
    std::string queuePath_;
    std::unique_ptr<IProductStore> managedQueue_; 
    std::unique_ptr<IQueueCursor> cursor_; 

    int aflags_{PqFlags::Default};
    bool create_{false};
    bool watch_flag_{false};
    bool clearMinVirtResTime_{false};
    off_t initialsz_{0};
    size_t align_{0};
    size_t nproducts_{0};
    int seqnum_{0};
    bool tty_flag_{false};

    Cmd currentCmd_{Cmd::BAD};
    Opt currentOpt_{Opt::NONE};
    std::string currentVal_;
    struct termios save_termios_{};

    FeedType fdtype_{ANY};
    ProdClass clss_;
    Product prodrec_;
    Match cdir_{Match::Equal};
    
    bool in_progress_{false};
    pqe_index idx_{};
    std::vector<uint8_t> activePayloadBuffer_;
    size_t prod_sz_{0};
    size_t prod_cnt_{0};
    std::unique_ptr<IQueueEntry> activeEntry_; 

    void exit_prog() {
        cursor_.reset();
        managedQueue_->close();
        exit(EXIT_SUCCESS);
    }

    int parse_time(const std::string& in_string, struct timeval* result) {
        if (in_string == "0") {
            *result = Timestamp::ZERO.ToTimeval();
            return 0;
        } else if (in_string == "EOT") {
            *result = Timestamp::ENDT.ToTimeval();
            return 0;
        } else if (in_string == "NOW") {
            gettimeofday(result, nullptr);
            return 0;
        } else {
            struct tm time_conv;
            int ret_val = std::sscanf(in_string.c_str(), "%4d%2d%2d%2d%2d",
                                      &time_conv.tm_year, &time_conv.tm_mon,
                                      &time_conv.tm_mday, &time_conv.tm_hour,
                                      &time_conv.tm_min);
            if (ret_val != 5) {
                LogError("Bad time string: {}", in_string);
                return 1;
            }
            time_conv.tm_sec = 0;
            time_conv.tm_year -= 1900;
            time_conv.tm_mon--;
            time_conv.tm_isdst = -1;
            result->tv_sec = mktime(&time_conv);
            return 0;
        }
    }

    int set_stdin(int vmin) {
        struct termios buf;
        if (tcgetattr(STDIN_FILENO, &save_termios_) < 0) return -1;
        buf = save_termios_;
        buf.c_lflag &= ~(ICANON | IEXTEN | ECHONL | ISIG);
        buf.c_iflag &= ~(BRKINT | INPCK | ISTRIP | IXON);
        buf.c_cflag &= ~(CSIZE | PARENB);
        buf.c_cflag |= CS8;
        buf.c_cc[VMIN] = vmin;
        buf.c_cc[VTIME] = 0;
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &buf) < 0) return -1;
        return 0;
    }

    int reset_stdin() {
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &save_termios_) < 0) return -1;
        return 0;
    }

    void init_options() {
        struct timeval tv;
        if (clss_.specs.empty()) clss_.specs.push_back({fdtype_, ".*"});
        else clss_.specs[0].feedtype = fdtype_;
        LogInfo("Feedtype set to {}", clss_.specs[0].feedtype.ToString());

        clss_.specs[0].pattern = ".*";
        LogInfo("Pattern set to \".*\"");

        clss_.from_sec = Timestamp::ZERO.tv_sec;
        clss_.from_usec = Timestamp::ZERO.tv_usec;
        time_t f_t = clss_.from_sec;
        LogInfo("From time set to {}", ctime(&f_t));

        clss_.to_sec = Timestamp::ENDT.tv_sec;
        clss_.to_usec = Timestamp::ENDT.tv_usec;
        time_t t_t = clss_.to_sec;
        LogInfo("To time set to {}", ctime(&t_t));

        parse_time("NOW", &tv);
        cursor_->setCursor(Timestamp::FromTimeval(tv));
        LogInfo("Cursor set to {}", ctime(&tv.tv_sec));

        cdir_ = Match::GreaterThan;
        LogInfo("Cursor direction set to Match::GreaterThan");

        prodrec_.info.origin = network::GetLocalHostName();

        parse_time("NOW", &tv);
        prodrec_.info.arrival.tv_sec = tv.tv_sec;
        prodrec_.info.arrival.tv_usec = tv.tv_usec;
        prodrec_.info.feedtype = EXP;
        prodrec_.info.seqno = seqnum_++;
        prodrec_.info.ident = "TEST PRODUCT";

        LogInfo("info.origin: {}", prodrec_.info.origin);
        time_t arr_t = prodrec_.info.arrival.tv_sec;
        LogInfo("info.arrival: {}", ctime(&arr_t));
        LogInfo("info.feedtype: EXP");
        LogInfo("info.seqno: {}", prodrec_.info.seqno);
        LogInfo("info.ident: {}", prodrec_.info.ident);
    }

    void parse_command(const std::string& cmdLine) {
        std::istringstream iss(cmdLine);
        std::string verb;
        iss >> verb;

        static const std::unordered_map<std::string, Cmd> cmdMap = {
            {"set", Cmd::SET}, {"show", Cmd::SHOW}, {"read", Cmd::READ},
            {"new", Cmd::NEW}, {"put", Cmd::PUT}, {"write", Cmd::WRITE},
            {"discard", Cmd::DISCARD}, {"display", Cmd::DISPLAY},
            {"watch", Cmd::WATCH}, {"delete", Cmd::DELETE},
            {"stats", Cmd::STATS}, {"help", Cmd::HELP}, {"quit", Cmd::QUIT}
        };

        static const std::unordered_map<std::string, Opt> optMap = {
            {"feedtype", Opt::FEEDTYPE}, {"pattern", Opt::PATTERN},
            {"cursor", Opt::CURSOR}, {"direction", Opt::CURSDIR},
            {"totime", Opt::TOTIME}, {"fromtime", Opt::FROMTIME},
            {"arrival", Opt::ARRIVAL}, {"prodftype", Opt::PRODFTYPE},
            {"seqno", Opt::SEQNO}, {"origin", Opt::ORIGIN},
            {"ident", Opt::IDENT}
        };

        auto it = cmdMap.find(verb);
        if (it != cmdMap.end()) {
            currentCmd_ = it->second;
        } else if (!verb.empty() && verb[0] == '#') {
            currentCmd_ = Cmd::COMMENT;
            return;
        } else {
            currentCmd_ = Cmd::BAD;
            return;
        }

        currentOpt_ = Opt::NONE;
        currentVal_.clear();

        if (currentCmd_ == Cmd::SET || currentCmd_ == Cmd::SHOW) {
            std::string optStr;
            iss >> optStr;
            auto optIt = optMap.find(optStr);
            if (optIt != optMap.end()) {
                currentOpt_ = optIt->second;
            } else {
                currentOpt_ = Opt::BAD;
                currentCmd_ = Cmd::BAD;
                LogError("Invalid option: {}", optStr);
                return;
            }
        }

        if (currentCmd_ == Cmd::SET || currentCmd_ == Cmd::READ ||
            currentCmd_ == Cmd::NEW || currentCmd_ == Cmd::PUT ||
            currentCmd_ == Cmd::DISPLAY || currentCmd_ == Cmd::DELETE) {
            
            std::getline(iss >> std::ws, currentVal_);
            if (currentVal_.empty() && currentCmd_ != Cmd::DELETE) {
                LogError("Command requires a trailing value");
                currentCmd_ = Cmd::BAD;
                currentOpt_ = Opt::BAD;
            }
        }
    }

    void print_help() {
        fmt::print(stdout, "\nset <option> <value>\n");
        fmt::print(stdout, "  options:  feedtype   [any valid LDM feedtype]\n");
        fmt::print(stdout, "            pattern   [any valid regular expression]\n");
        fmt::print(stdout, "            cursor    [0|NOW|EOT|yyyymmddhhmm]\n");
        fmt::print(stdout, "            direction [LT|EQ|GT]\n");
        fmt::print(stdout, "            totime    [0|NOW|EOT|yyyymmddhhmm]\n");
        fmt::print(stdout, "            fromtime  [0|NOW|EOT|yyyymmddhhmm]\n");
        fmt::print(stdout, "            arrival   [0|NOW|EOT|yyyymmddhhmm]\n");
        fmt::print(stdout, "            prodftype [any valid LDM feedtype]\n");
        fmt::print(stdout, "            seqno     [any positive integer]\n");
        fmt::print(stdout, "            origin    [hostname string]\n");
        fmt::print(stdout, "            ident     [product id]\n");
        fmt::print(stdout, "show <option>\n");
        fmt::print(stdout, "  options: same as for set command\n");
        fmt::print(stdout, "read <filename> (use - for stdin)\n");
        fmt::print(stdout, "new <product size>\n");
        fmt::print(stdout, "put <filename> (use - for stdin)\n");
        fmt::print(stdout, "write\n");
        fmt::print(stdout, "discard\n");
        fmt::print(stdout, "display <filename> (use - for stdout)\n");
        fmt::print(stdout, "watch\n");
        fmt::print(stdout, "delete totime [0|NOW|EOT|yyyymmddhhmm]\n");
        fmt::print(stdout, "stats\n");
        fmt::print(stdout, "quit\n\n");
    }

    void print_stats() {
        off_t highwater;
        size_t maxprods;
        fmt::print(stdout, "\nSystem Page Size:\t{:>20}\n", managedQueue_->getPageSize());
        fmt::print(stdout, "Queue Page Size:\t{:>20}\n", managedQueue_->getPageSize());
        managedQueue_->getHighwater(highwater, maxprods);
        fmt::print(stdout, "Maximum Bytes Used:\t{:>20}\n", static_cast<long>(highwater));
        fmt::print(stdout, "Maximum Products Held:\t{:>20}\n\n", static_cast<unsigned long>(maxprods));
    }

    void set_option() {
        FeedType ftype;
        int result;
        struct timeval curstime;

        switch (currentOpt_) {
        case Opt::FEEDTYPE:
            if ((result = FeedType::Parse(currentVal_, ftype)) != FEEDTYPE_OK) {
                LogError("set_option: {}: {}", currentVal_, FeedType::GetParseErrorMsg(result));
                break;
            }
            clss_.specs[0].feedtype = ftype;
            LogInfo("Feedtype set to {}", currentVal_);
            break;
        case Opt::PATTERN:
            clss_.specs[0].pattern = currentVal_;
            LogInfo("Pattern set to \"{}\"", currentVal_);
            break;
        case Opt::CURSOR:
            if (parse_time(currentVal_, &curstime)) {
                LogError("Invalid cursor time specification: {}", currentVal_);
                break;
            }
            cursor_->setCursor(Timestamp::FromTimeval(curstime));
            LogInfo("Cursor time set to {}", ctime(&curstime.tv_sec));
            break;
        case Opt::CURSDIR:
            if (currentVal_ == "LT") {
                cdir_ = Match::LessThan;
                LogInfo("Cursor direction set to Match::LessThan");
            } else if (currentVal_ == "EQ") {
                cdir_ = Match::Equal;
                LogInfo("Cursor direction set to Match::Equal");
            } else if (currentVal_ == "GT") {
                cdir_ = Match::GreaterThan;
                LogInfo("Cursor direction set to Match::GreaterThan");
            } else {
                LogError("Invalid direction specified: {}", currentVal_);
            }
            break;
        case Opt::TOTIME:
            if (parse_time(currentVal_, &curstime)) {
                LogError("Invalid \"to\" time specification: {}", currentVal_);
                break;
            }
            clss_.to_sec = curstime.tv_sec;
            clss_.to_usec = curstime.tv_usec;
            {
                time_t t = clss_.to_sec;
                LogInfo("To time set to {}", ctime(&t));
            }
            break;
        case Opt::FROMTIME:
            if (parse_time(currentVal_, &curstime)) {
                LogError("Invalid \"from\" time specification: {}", currentVal_);
                break;
            }
            clss_.from_sec = curstime.tv_sec;
            clss_.from_usec = curstime.tv_usec;
            {
                time_t t = clss_.from_sec;
                LogInfo("From time set to {}", ctime(&t));
            }
            break;
        case Opt::ARRIVAL:
            if (parse_time(currentVal_, &curstime)) {
                LogError("Invalid \"arrival\" time specification: {}", currentVal_);
                break;
            }
            prodrec_.info.arrival.tv_sec = curstime.tv_sec;
            prodrec_.info.arrival.tv_usec = curstime.tv_usec;
            {
                time_t arr_t = prodrec_.info.arrival.tv_sec;
                LogInfo("Arrival time set to {}", ctime(&arr_t));
            }
            break;
        case Opt::PRODFTYPE:
            if ((result = FeedType::Parse(currentVal_, ftype)) != FEEDTYPE_OK) {
                LogError("set_option: {}: {}", currentVal_, FeedType::GetParseErrorMsg(result));
                break;
            }
            prodrec_.info.feedtype = ftype;
            LogInfo("Feedtype set to {}", currentVal_);
            break;
        case Opt::SEQNO:
            seqnum_ = std::stoi(currentVal_);
            prodrec_.info.seqno = seqnum_++;
            LogInfo("Sequence Number set to {}", prodrec_.info.seqno);
            break;
        case Opt::ORIGIN:
            prodrec_.info.origin = currentVal_;
            LogInfo("Origin set to {}", prodrec_.info.origin);
            break;
        case Opt::IDENT:
            prodrec_.info.ident = currentVal_;
            LogInfo("Product ID set to {}", prodrec_.info.ident);
            break;
        default:
            break;
        }
    }

    void show_option() {
        Timestamp clean_tv;
        time_t t;
        switch (currentOpt_) {
        case Opt::FEEDTYPE:
            fmt::print(stdout, "Feedtype is {}\n", clss_.specs[0].feedtype.ToString());
            break;
        case Opt::PATTERN:
            fmt::print(stdout, "Pattern is \"{}\"\n", clss_.specs[0].pattern);
            break;
        case Opt::CURSOR:
            cursor_->getCursorTimestamp(clean_tv);
            t = clean_tv.tv_sec;
            fmt::print(stdout, "Cursor time is {}", ctime(&t));
            break;
        case Opt::CURSDIR:
            if (cdir_ == Match::LessThan) fmt::print(stdout, "Cursor direction is Match::LessThan\n");
            else if (cdir_ == Match::Equal) fmt::print(stdout, "Cursor direction is Match::Equal\n");
            else fmt::print(stdout, "Cursor direction is Match::GreaterThan\n");
            break;
        case Opt::TOTIME:
            t = clss_.to_sec;
            fmt::print(stdout, "To time set to {}", ctime(&t));
            break;
        case Opt::FROMTIME:
            t = clss_.from_sec;
            fmt::print(stdout, "From time set to {}", ctime(&t));
            break;
        case Opt::ARRIVAL:
            t = prodrec_.info.arrival.tv_sec;
            fmt::print(stdout, "Arrival time set to {}", ctime(&t));
            break;
        case Opt::PRODFTYPE:
            fmt::print(stdout, "Product feedtype is {}\n", prodrec_.info.feedtype.ToString());
            break;
        case Opt::SEQNO:
            fmt::print(stdout, "Seqence number is {}\n", seqnum_ - 1);
            break;
        case Opt::ORIGIN:
            fmt::print(stdout, "Product origin is {}\n", prodrec_.info.origin);
            break;
        case Opt::IDENT:
            fmt::print(stdout, "Product ID is \"{}\"\n", prodrec_.info.ident);
            break;
        default:
            break;
        }
    }

    void read_file() {
        if (currentVal_ == "-") {
            int ch;
            int bufcnt = 0;
            std::vector<char> buffer(DBUFMAX);
            if (tty_flag_) {
                fmt::print(stdout, "Enter product (^D when finished)\n");
                if (set_stdin(1)) {
                    LogError("set_stdin: noncanonical mode set failed");
                    return;
                }
            }
            while ((ch = getc(stdin)) != '\004') {
                if (bufcnt >= DBUFMAX) {
                    LogError("Product must be smaller than {} bytes", DBUFMAX);
                    LogError("Read operation aborted");
                    return;
                }
                buffer[bufcnt] = static_cast<char>(ch);
                bufcnt++;
            }
            prodrec_.info.sz = bufcnt;
            prodrec_.info.signature = Signature::GenerateMD5(buffer.data(), prodrec_.info.sz);
            prodrec_.data = reinterpret_cast<const uint8_t*>(buffer.data());

            int status = managedQueue_->insert(prodrec_);
            if (status != 0 && status != static_cast<int>(PqStatus::Dup)) {
                LogError("pq.insert returned {}", status);
                return;
            }
            prodrec_.info.seqno = seqnum_++;
            if (tty_flag_) {
                if (reset_stdin()) LogError("reset_stdin: stdin reset failed");
                fmt::print(stdout, "\n");
            }
        } else {
            std::ifstream file(currentVal_, std::ios::binary | std::ios::ate);
            if (!file.is_open()) {
                LogSyserr("Error opening {}", currentVal_);
                return;
            }
            std::streamsize size = file.tellg();
            file.seekg(0, std::ios::beg);
            LogDebug("read_file: File size is {}", size);

            if (size <= DBUFMAX) {
                prodrec_.info.sz = size;
                std::vector<char> buffer(size);
                if (!file.read(buffer.data(), size)) {
                    LogSyserr("Bad file read: {}", currentVal_);
                    return;
                }
                //compute_md5(buffer.data(), size, prodrec_.info.signature);
                prodrec_.info.signature = Signature::GenerateMD5(buffer.data(), prodrec_.info.sz);
                prodrec_.data = reinterpret_cast<const uint8_t*>(buffer.data());
                
                int status = managedQueue_->insert(prodrec_);
                if (status) {
                    LogError("pq.insert() returned {}", status);
                    return;
                }
                prodrec_.info.seqno = seqnum_++;
            } else {
                prodrec_.info.sz = size;
                std::vector<char> buffer(size);
                if (!file.read(buffer.data(), size)) {
                    LogSyserr("Bad file read: {}", currentVal_);
                    return;
                }
                prodrec_.info.signature = Signature::GenerateMD5(buffer.data(), prodrec_.info.sz);
                
                std::unique_ptr<IQueueEntry> entry;
                int status = managedQueue_->newElement(prodrec_.info, entry);
                if (status) {
                    LogError("pq.newElement() returned {}", status);
                    return;
                }
                std::memcpy(entry->getPayloadPointer(), buffer.data(), size);
                status = entry->commit();
                if (status) {
                    LogError("entry.commit() returned {}", status);
                    return;
                }
                prodrec_.info.seqno = seqnum_++;
            }
        }
    }

    void big_product() {
        int status;
        switch (currentCmd_) {
        case Cmd::NEW: {
            if (in_progress_) {
                LogError("Must complete old product first");
                return;
            }
            prod_sz_ = std::stoull(currentVal_);
            prodrec_.info.sz = prod_sz_;
            
            status = managedQueue_->newElement(prodrec_.info, activeEntry_);
            if (status) {
                LogError("pq.newElement() returned {}", status);
                return;
            }
            activePayloadBuffer_.resize(prod_sz_);
            in_progress_ = true;
            prod_cnt_ = 0;
            break;
        }
        case Cmd::PUT: {
            if (!in_progress_) {
                LogError("Must start product with \"new\" command");
                return;
            }
            if (currentVal_ == "-") {
                int ch;
                if (tty_flag_) fmt::print(stdout, "Enter product piece (^D when finished): ");
                while ((ch = fgetc(stdin)) != EOF) {
                    if (++prod_cnt_ > prod_sz_) {
                        LogError("product exceeds buffer size: aborting input");
                        (void)fflush(stdin);
                        prod_cnt_--;
                        return;
                    }
                    activePayloadBuffer_[prod_cnt_ - 1] = ch;
                }
            } else {
                std::ifstream file(currentVal_, std::ios::binary | std::ios::ate);
                if (!file.is_open()) {
                    LogSyserr("Error opening {}", currentVal_);
                    return;
                }
                std::streamsize fileSize = file.tellg();
                file.seekg(0, std::ios::beg);
                if ((prod_cnt_ + fileSize) > prod_sz_) {
                    LogError("Product exceeds allocated size: input aborted");
                    return;
                }
                if (!file.read(reinterpret_cast<char*>(activePayloadBuffer_.data() + prod_cnt_), fileSize)) {
                    LogSyserr("Bad file read");
                    return;
                }
                prod_cnt_ += fileSize;
            }
            break;
        }
        case Cmd::WRITE:
            if (!in_progress_) {
                LogError("No product currently in progress to write");
                return;
            }
            std::memcpy(activeEntry_->getPayloadPointer(), activePayloadBuffer_.data(), prod_sz_);
            prodrec_.info.signature = Signature::GenerateMD5(activeEntry_->getPayloadPointer(), prod_sz_);
            status = activeEntry_->commit();
            if (status) {
                LogError("big_product(): activeEntry_.commit() failure: {}", status);
                return;
            }
            in_progress_ = false;
            prod_cnt_ = 0;
            activePayloadBuffer_.clear();
            break;
        case Cmd::DISCARD:
            if (!in_progress_) {
                LogError("No product in progress: nothing to discard");
                return;
            }
            status = activeEntry_->rollback();
            if (status) {
                LogError("activeEntry_.rollback: {}", status);
                return;
            }
            in_progress_ = false;
            prod_cnt_ = 0;
            activePayloadBuffer_.clear();
            break;
        default:
            break;
        }
    }

    static int disp_stdout(const ProdInfo& infop, const void* datap, void* xprod, size_t size_notused, void* notused) {
        if (write(STDOUT_FILENO, datap, infop.sz) != static_cast<ssize_t>(infop.sz)) {
            int errnum = errno;
            LogSyserr("data write failed");
            return errnum;
        }
        fmt::print(stdout, "\n");
        return 0;
    }

    static int disp_file(const ProdInfo& infop, const void* datap, void* xprod, size_t size_notused, void* arg) {
        auto* app = static_cast<PqUtilApp*>(arg);
        std::ofstream ofs(app->currentVal_, std::ios::binary | std::ios::app);
        if (!ofs.is_open()) {
            int errnum = errno;
            LogSyserr("Error opening {}", app->currentVal_);
            return errnum;
        }
        if (!ofs.write(static_cast<const char*>(datap), infop.sz)) {
            int errnum = errno;
            LogSyserr("data write failed");
            return errnum;
        }
        return 0;
    }

    void display_product() {
        int status = 0;
        while (1) {
            status = (currentVal_ != "-")
                ? cursor_->sequence(cdir_, clss_, disp_file, this)
                : cursor_->sequence(cdir_, clss_, disp_stdout, nullptr);
                
            if (status == 0) continue;
            
            if (status == static_cast<int>(PqStatus::End)) {
                LogDebug("End of queue");
            } else if (status > 0) {
                LogError("pq_sequence: {}", std::strerror(status));
            }
            break;
        }
    }

    static int display_watch(const ProdInfo& infop, const void* datap, void* xprod, size_t size_notused, void* notused) {
        unsigned int logLevel = log_get_level();
        (void)log_set_level(LOG_LEVEL_INFO);
        if (log_is_enabled_info) {
            LogInfo("{}", infop.ToString(false));
        }
        (void)log_set_level(static_cast<log_level_t>(logLevel));
        return 0;
    }

    void watch_queue() {
        int ch;
        int status;
        int keep_at_it = 1;
        Timestamp tvout;
        int ifd = fileno(stdin);
        int ready;
        int width = (1 << ifd);
        fd_set readfds;
        struct timeval timeo;

        clss_.from_sec = Timestamp::ZERO.tv_sec;
        clss_.from_usec = Timestamp::ZERO.tv_usec;
        clss_.to_sec = Timestamp::ENDT.tv_sec;
        clss_.to_usec = Timestamp::ENDT.tv_usec;
        
        time_t f_t = clss_.from_sec;
        time_t t_t = clss_.to_sec;
        LogInfo("From time set to {}", ctime(&f_t));
        LogInfo("To time set to {}", ctime(&t_t));
        
        cursor_->setCursorToLast(clss_, tvout);

        if (tty_flag_) {
            fmt::print(stdout, "(Type ^D when finished)\n");
            if (set_stdin(0)) {
                LogError("set_stdin: noncanonical mode set failed");
                return;
            }
        }

        while (keep_at_it) {
            FD_ZERO(&readfds);
            FD_SET(ifd, &readfds);
            timeo.tv_sec = 1;
            timeo.tv_usec = 0;
            ready = select(width, &readfds, 0, 0, &timeo);
            
            if (ready < 0) {
                if (errno == EINTR) {
                    errno = 0;
                    continue;
                }
                LogSyserr("select");
                exit(1);
            }
            
            if (ready > 0) {
                if (FD_ISSET(ifd, &readfds)) {
                    ch = getc(stdin);
                    if (ch != ' ' && ch != '\n') {
                        while (1) {
                            status = cursor_->sequence(Match::GreaterThan, clss_, display_watch, nullptr);
                            if (status == 0) continue;
                            else if (status == static_cast<int>(PqStatus::End)) {
                                LogDebug("End of queue");
                                break;
                            } else {
                                fmt::print(stdout, "status: {}\n", status);
                                LogError("pq.sequence: {}", std::strerror(status));
                                return;
                            }
                        }
                    } else {
                        keep_at_it = 0;
                    }
                }
            } else {
                while (1) {
                    status = cursor_->sequence(Match::GreaterThan, clss_, display_watch, nullptr);
                    if (status == 0) continue;
                    else if (status == static_cast<int>(PqStatus::End)) {
                        LogDebug("End of queue");
                        break;
                    } else {
                        fmt::print(stdout, "status: {}\n", status);
                        LogError("pq.sequence: {}", std::strerror(status));
                        return;
                    }
                }
            }
        }
        if (tty_flag_) {
            if (reset_stdin()) LogError("reset_stdin: failed");
            fmt::print(stdout, "\n");
        }
    }

    void rm_prod() {
        int status;
        size_t savail;
        Timestamp ins_time;
        while (1) {
            status = cursor_->sequenceDelete(cdir_, clss_, 0, savail, ins_time);
            switch (status) {
            case 0: {
                time_t t = ins_time.tv_sec;
                LogInfo("{} bytes freed: {}", savail, ctime(&t));
                break;
            }
            case static_cast<int>(PqStatus::End):
                LogDebug("End of queue");
                return;
            default:
                LogError("pq.sequenceDelete: {}", std::strerror(status));
                return;
            }
        }
    }

protected:
    void ConfigureOptions() override {
        Application::ConfigureOptions();
        RegisterFlag('c', "Create the pqueue, clobber existing");
        RegisterFlag('n', "Create the pqueue, error if it exists");
        RegisterFlag('r', "Open read only");
        RegisterFlag('P', "Open private (PqFlags::Private)");
        RegisterFlag('L', "Open no locking (PqFlags::NoLock)");
        RegisterFlag('F', "Open 'fixed size' (PqFlags::NoGrow)");
        RegisterFlag('M', "Open PqFlags::NoMap");
        RegisterFlag('w', "Run the watch command and exit when through");
        RegisterFlag('C', "Clear the minimum virtual residence time metrics and exit");
        RegisterOption('a', "align", "Align (round up) allocations to 'align' boundaries", "0");
        RegisterOption('s', "size", "Initial data section size", "0");
        RegisterOption('S', "slots", "Initially allocate index space for 'nproducts' products", "0");
        RegisterOption('f', "feedtype", "Product feedtype (default ANY)", "ANY");
        RegisterFlag('p', "Align (round up) allocations to the pagesize");
    }

    bool ProcessOptions() override {
        if (!Application::ProcessOptions()) return false;
        
        if (IsSet('c')) create_ = true;
        if (IsSet('n')) {
            create_ = true;
            aflags_ |= PqFlags::NoClobber;
        }
        if (IsSet('a')) align_ = std::stoull(GetOption('a'));
        if (IsSet('S')) nproducts_ = std::stoull(GetOption('S'));
        if (IsSet('s')) {
            std::string s_val = GetOption('s');
            char* cp = const_cast<char*>(s_val.c_str()) + s_val.length() - 1;
            initialsz_ = std::stoull(s_val);
            if (std::isalpha(*cp)) {
                switch (*cp) {
                case 'k': case 'K': initialsz_ *= 1024; break;
                case 'm': case 'M': initialsz_ += (1024 * 1024); break;
                default: initialsz_ = 0; break;
                }
            }
        }
        if (IsSet('p')) align_ = managedQueue_->getPageSize();
        if (IsSet('r')) aflags_ |= PqFlags::ReadOnly;
        if (IsSet('P')) aflags_ &= PqFlags::Private;
        if (IsSet('L')) aflags_ &= PqFlags::NoLock;
        if (IsSet('F')) aflags_ |= PqFlags::NoGrow;
        if (IsSet('M')) aflags_ |= PqFlags::NoMap;
        watch_flag_ = IsSet('w');
        clearMinVirtResTime_ = IsSet('C');
        
        if (IsSet('f')) {
            if (FeedType::Parse(GetOption('f'), fdtype_) != FEEDTYPE_OK) {
                LogError("Invalid feedtype: {}", GetOption('f'));
                return false;
            }
        }
        if (!positionalArgs_.empty()) {
            queuePath_ = positionalArgs_[0];
            registry::setQueuePath(queuePath_);
        } else {
            queuePath_ = registry::getQueuePath();
        }
        return true;
    }

    int Run() override {
        putenv(const_cast<char*>("TZ=GMT"));
        tty_flag_ = isatty(fileno(stdin)) != 0;
        int status = 0;

        if (!create_) {
            status = managedQueue_->open(queuePath_, aflags_);
            if (status != 0) {
                if (static_cast<int>(PqStatus::Corrupt) == status) {
                    LogError("The product-queue \"{}\" is inconsistent", queuePath_);
                } else {
                    LogError("pq_open: {}: {}", queuePath_, std::strerror(status));
                }
                return EXIT_FAILURE;
            }
        } else {
            status = managedQueue_->create(queuePath_, 0666, aflags_, align_, initialsz_, nproducts_);
            if (status != 0) {
                LogError("pq_create: {}: {}", queuePath_, std::strerror(status));
                return EXIT_FAILURE;
            }
        }

        // NEW: Instantiate local cursor
        cursor_ = managedQueue_->CreateCursor();

        init_options();

        if (watch_flag_) {
            watch_queue();
            return EXIT_SUCCESS;
        }

        if (clearMinVirtResTime_) {
            status = managedQueue_->clearMinVirtResTimeMetrics();
            if (status) {
                LogError("Couldn't clear minimum virtual residence time metrics: {}", std::strerror(status));
            }
            return status;
        }

        std::string commandLine;
        if (tty_flag_) fmt::print(stdout, "Enter Command --> ");
        
        while (std::getline(std::cin, commandLine)) {
            if (!commandLine.empty()) {
                parse_command(commandLine);
                switch (currentCmd_) {
                case Cmd::SET:     set_option(); break;
                case Cmd::SHOW:    show_option(); break;
                case Cmd::READ:    read_file(); break;
                case Cmd::NEW:     big_product(); break;
                case Cmd::PUT:     big_product(); break;
                case Cmd::WRITE:   big_product(); break;
                case Cmd::DISCARD: big_product(); break;
                case Cmd::DISPLAY: display_product(); break;
                case Cmd::WATCH:   watch_queue(); break;
                case Cmd::DELETE:  rm_prod(); break;
                case Cmd::STATS:   print_stats(); break;
                case Cmd::HELP:    print_help(); break;
                case Cmd::QUIT:    exit_prog(); break;
                case Cmd::COMMENT: break;
                case Cmd::BAD:     LogError("Bad command (type help for command list): {}", commandLine); break;
                }
            }
            if (tty_flag_) fmt::print(stdout, "Enter Command --> ");
        }
        return EXIT_SUCCESS;
    }

public:
    PqUtilApp() : Application("Interactive utility for inspecting and managing LDM product queues."),
                  managedQueue_(StorageFactory::Create(NetworkFactory::CreateSerializer())) {} 
};

int main(int argc, char *argv[]) {
    PqUtilApp app;
    return app.Execute(argc, argv);
}
