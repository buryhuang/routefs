#ifndef PERFTIMER_H
#define PERFTIMER_H

#include <time.h>

/**
 * @brief this class is used to time in nanosecond degree for performance test
 */
class PerfTimer
{
public:
    /**
     * @brief constructor
     */
    PerfTimer()
        : m_isRunning(false)
        , m_since(0)
        , m_accrued(0)
    {
    }

    /**
     * @brief destructor
     */
    ~PerfTimer()
    {
    }

    /**
     * @brief start timer
     */
    void start()
    {
        if (!m_isRunning) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            m_since = now.tv_sec * 1000000000 + now.tv_nsec;
            m_isRunning = true;
        }
    }

    /**
     * @brief stop timer
     */
    void stop()
    {
        if (m_isRunning) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            m_accrued += now.tv_sec * 1000000000 + now.tv_nsec - m_since;
            m_isRunning = false;
        }
    }

    /**
     * @brief get the accrued nanoseconds
     *
     * @return the nanoseconds in integer
     */
    sts_uint64_t nanoseconds() const
    {
        sts_uint64_t time = m_accrued;
        if (m_isRunning) {
            struct timespec now;
            clock_gettime(CLOCK_MONOTONIC, &now);
            time += now.tv_sec * 1000000000 + now.tv_nsec - m_since;
        }

        return time;
    }

    /**
     * @brief get the accrued microseconds
     *
     * @return the microseconds in double
     */
    double microseconds() const
    {
        return double(nanoseconds()) / 1000;
    }

    /**
     * @brief get the accrued milliseconds
     *
     * @return the milliseconds in double
     */
    double milliseconds() const
    {
        return double(nanoseconds()) / 1000000;
    }

    /**
     * @brief get the timed seconds
     *
     * @return the seconds number in double.
     */
    double seconds() const
    {
        return double(nanoseconds()) / 1000000000;
    }

    /**
     * @brief reset timer
     */
    void reset()
    {
        m_isRunning = false;
        m_since = 0;
        m_accrued = 0;
    }

private:
    //! disable copy constructor and operator=
    PerfTimer(const PerfTimer&);
    const PerfTimer& operator=(const PerfTimer&);

private:
    bool            m_isRunning;
    sts_uint64_t    m_since;
    sts_uint64_t    m_accrued;

};

#endif

