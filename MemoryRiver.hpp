#ifndef BPT_MEMORYRIVER_HPP
#define BPT_MEMORYRIVER_HPP

#include <fstream>
#include <string>

using std::string;
using std::fstream;
using std::ifstream;
using std::ofstream;

// A simple file-backed storage with optional space reclamation.
// Stores user info_len ints at file head, and one extra int for free-list head.

template<class T, int info_len = 2>
class MemoryRiver {
private:
    // One extra int after the first info_len for free list head (byte offset). 0 means empty.
    static constexpr int EXTRA_LEN = 1;

    fstream file;
    string file_name;
    int sizeofT = sizeof(T);

    std::streamoff user_header_bytes() const { return static_cast<std::streamoff>(info_len) * sizeof(int); }
    std::streamoff full_header_bytes() const { return static_cast<std::streamoff>(info_len + EXTRA_LEN) * sizeof(int); }

    bool ensure_open() {
        if (file.is_open()) return true;
        file.open(file_name, std::ios::in | std::ios::out | std::ios::binary);
        if (!file.is_open()) {
            // Create new file with header (info_len + EXTRA_LEN) ints initialized to 0.
            ofstream init(file_name, std::ios::binary | std::ios::trunc);
            if (!init.is_open()) return false;
            int zero = 0;
            for (int i = 0; i < info_len + EXTRA_LEN; ++i) init.write(reinterpret_cast<char*>(&zero), sizeof(int));
            init.close();
            file.open(file_name, std::ios::in | std::ios::out | std::ios::binary);
        }
        if (!file.is_open()) return false;
        // Ensure extra header exists (append zeros if only user header exists)
        file.seekg(0, std::ios::end);
        std::streamoff sz = file.tellg();
        if (sz < full_header_bytes()) {
            file.clear();
            file.seekp(0, std::ios::end);
            int zero = 0;
            // Append until full_header_bytes reached
            while (sz < full_header_bytes()) {
                file.write(reinterpret_cast<char*>(&zero), sizeof(int));
                sz += sizeof(int);
            }
            file.flush();
        }
        return true;
    }

    // Read/write the free-list head (byte offset) stored right after user info ints.
    int read_free_head() {
        if (!ensure_open()) return 0;
        file.clear();
        file.seekg(user_header_bytes(), std::ios::beg);
        int head = 0;
        file.read(reinterpret_cast<char*>(&head), sizeof(int));
        return head;
    }

    void write_free_head(int head) {
        if (!ensure_open()) return;
        file.clear();
        file.seekp(user_header_bytes(), std::ios::beg);
        file.write(reinterpret_cast<char*>(&head), sizeof(int));
        file.flush();
    }

public:
    MemoryRiver() = default;

    MemoryRiver(const string& file_name) : file_name(file_name) {}

    void initialise(string FN = "") {
        if (FN != "") file_name = FN;
        file.open(file_name, std::ios::out | std::ios::binary | std::ios::trunc);
        int tmp = 0;
        for (int i = 0; i < info_len; ++i)
            file.write(reinterpret_cast<char *>(&tmp), sizeof(int));
        // Ensure our extra header exists as zeros as well
        for (int i = 0; i < EXTRA_LEN; ++i)
            file.write(reinterpret_cast<char *>(&tmp), sizeof(int));
        file.close();
    }

    // Read the n-th (1-based) user info int into tmp
    void get_info(int &tmp, int n) {
        if (n > info_len) return;
        if (!ensure_open()) return;
        file.clear();
        std::streamoff pos = static_cast<std::streamoff>(n - 1) * sizeof(int);
        file.seekg(pos, std::ios::beg);
        file.read(reinterpret_cast<char*>(&tmp), sizeof(int));
    }

    // Write tmp into the n-th (1-based) user info int
    void write_info(int tmp, int n) {
        if (n > info_len) return;
        if (!ensure_open()) return;
        file.clear();
        std::streamoff pos = static_cast<std::streamoff>(n - 1) * sizeof(int);
        file.seekp(pos, std::ios::beg);
        file.write(reinterpret_cast<char*>(&tmp), sizeof(int));
        file.flush();
    }

    // Write object t at a suitable position; return its starting byte offset as index
    int write(T &t) {
        if (!ensure_open()) return -1;
        // Try to reuse from free list
        int head = read_free_head();
        if (head != 0) {
            // Read next free from start of this slot (overlaid as int)
            int next = 0;
            file.clear();
            file.seekg(static_cast<std::streamoff>(head), std::ios::beg);
            file.read(reinterpret_cast<char*>(&next), sizeof(int));
            // Overwrite with T
            file.clear();
            file.seekp(static_cast<std::streamoff>(head), std::ios::beg);
            file.write(reinterpret_cast<char*>(&t), sizeofT);
            file.flush();
            write_free_head(next);
            return head;
        } else {
            // Append at EOF
            file.clear();
            file.seekp(0, std::ios::end);
            std::streamoff pos = file.tellp();
            // Ensure we do not place before header
            if (pos < full_header_bytes()) {
                pos = full_header_bytes();
                file.seekp(pos, std::ios::beg);
            }
            file.write(reinterpret_cast<char*>(&t), sizeofT);
            file.flush();
            return static_cast<int>(pos);
        }
    }

    // Update the object at given index
    void update(T &t, const int index) {
        if (!ensure_open()) return;
        file.clear();
        file.seekp(static_cast<std::streamoff>(index), std::ios::beg);
        file.write(reinterpret_cast<char*>(&t), sizeofT);
        file.flush();
    }

    // Read the object at given index into t
    void read(T &t, const int index) {
        if (!ensure_open()) return;
        file.clear();
        file.seekg(static_cast<std::streamoff>(index), std::ios::beg);
        file.read(reinterpret_cast<char*>(&t), sizeofT);
    }

    // Delete the object at given index (push it to free list)
    void Delete(int index) {
        if (!ensure_open()) return;
        // Write current free head into beginning of this slot, then set head to this index
        int head = read_free_head();
        file.clear();
        file.seekp(static_cast<std::streamoff>(index), std::ios::beg);
        file.write(reinterpret_cast<char*>(&head), sizeof(int));
        file.flush();
        write_free_head(index);
    }
};


#endif //BPT_MEMORYRIVER_HPP
