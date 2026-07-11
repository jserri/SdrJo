#pragma once
//
// Ring buffer single-producer/single-consumer senza lock, usato per passare
// campioni IQ dal thread di acquisizione USB ai thread DSP dei moduli.
//
#include <atomic>
#include <cstddef>
#include <vector>

namespace sdrjo {

template <typename T>
class RingBuffer {
public:
    explicit RingBuffer(size_t capacity)
        : buf_(capacity + 1), cap_(capacity + 1) {}

    // Scrive fino a n elementi; restituisce quanti ne sono entrati.
    size_t write(const T* data, size_t n)
    {
        size_t w = writePos_.load(std::memory_order_relaxed);
        size_t r = readPos_.load(std::memory_order_acquire);
        size_t free = (r + cap_ - w - 1) % cap_;
        if (n > free) n = free;
        for (size_t i = 0; i < n; i++)
            buf_[(w + i) % cap_] = data[i];
        writePos_.store((w + n) % cap_, std::memory_order_release);
        return n;
    }

    // Legge fino a n elementi; restituisce quanti ne sono usciti.
    size_t read(T* out, size_t n)
    {
        size_t r = readPos_.load(std::memory_order_relaxed);
        size_t w = writePos_.load(std::memory_order_acquire);
        size_t avail = (w + cap_ - r) % cap_;
        if (n > avail) n = avail;
        for (size_t i = 0; i < n; i++)
            out[i] = buf_[(r + i) % cap_];
        readPos_.store((r + n) % cap_, std::memory_order_release);
        return n;
    }

    size_t available() const
    {
        size_t r = readPos_.load(std::memory_order_acquire);
        size_t w = writePos_.load(std::memory_order_acquire);
        return (w + cap_ - r) % cap_;
    }

    // Scarta tutto il contenuto in attesa. Da chiamare SOLO dal thread
    // consumatore (avanza la lettura fino alla scrittura corrente): usato
    // al cambio di frequenza per non mostrare il backlog della vecchia.
    void clear()
    {
        readPos_.store(writePos_.load(std::memory_order_acquire),
                       std::memory_order_release);
    }

private:
    std::vector<T> buf_;
    size_t cap_;
    std::atomic<size_t> readPos_{0};
    std::atomic<size_t> writePos_{0};
};

} // namespace sdrjo
