#ifndef _SNARKLIB_PROGRESS_CALLBACK_HPP_
#define _SNARKLIB_PROGRESS_CALLBACK_HPP_

#include <cstdint>

namespace snarklib {

////////////////////////////////////////////////////////////////////////////////
// progress callback
//

class ProgressCallback
{
public:
    virtual ~ProgressCallback() = default;

    // number of majorProgress callback from PPZK keypair, proof, verify
    virtual void majorSteps(const std::size_t numberSteps) = 0;

    // callback from PPZK keypair, proof, verify
    virtual void majorProgress(const bool newLine = false) = 0;

    // number of minorProgress callbacks expected
    virtual std::size_t minorSteps() = 0;

    // callback inside function called from PPZK
    virtual void minorProgress() = 0;
};

// no operation callback
template <typename T>
class ProgressCallback_NOP : public ProgressCallback
{
public:
    void majorSteps(const std::size_t) {}
    void majorProgress(const bool newLine) {}

    std::size_t minorSteps() { return 0; }
    void minorProgress() {}
};

} // namespace snarklib

#endif
