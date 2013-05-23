#ifndef ___IceCredentials_hpp__
#define ___IceCredentials_hpp__

#include <IntTypes.hpp>
#include <WebRtcRelayUtils.hpp>
#include <boost/shared_ptr.hpp>
#include <vector>
#include <string>

enum
{
    ICE_DEFAULT_UFRAG_LEN = 16,
    ICE_DEFAULT_PWD_LEN = 24
};

class IceCredentials
{
public:
    explicit IceCredentials(boost::mt19937& gen)
    {
        generateLocal(gen);
    }

    void setRemoteCredentials(const std::string& ufrag, const std::string& pwd)
    {
        _remoteUfrag.assign(ufrag.begin(), ufrag.end());
        _remotePwd.assign(pwd.begin(), pwd.end());

        _verifyingUname.assign(_localUfrag.begin(), _localUfrag.end());
        _verifyingUname.push_back(':');
        _verifyingUname.insert(_verifyingUname.end(),
            _remoteUfrag.begin(), _remoteUfrag.end());
    }

    void getVerifyingCredentials(std::vector<sm_uint8_t>*& uname,
        std::vector<sm_uint8_t>*& pwd)
    {
        uname = &_verifyingUname;
        pwd = &_localPwd;
    }

    std::vector<sm_uint8_t> verifyingUname() const
    {
        return _verifyingUname;
    }

    std::vector<sm_uint8_t> verifyingPwd() const
    {
        return _localPwd;
    }

    void verifyingPwd(sm_uint8_t **password, size_t *passwordLen)
    {
        *password = &_localPwd[0];
        *passwordLen = _localPwd.size();
    }

    std::string localUfrag() const
    {
        std::string localUfrag(_localUfrag.begin(), _localUfrag.end());
        assert(localUfrag.length() == _localUfrag.size());
        assert(!localUfrag.empty());
        return localUfrag;
    }

    std::string localPwd() const
    {
        std::string localPwd(_localPwd.begin(), _localPwd.end());
        assert(localPwd.length() == _localPwd.size());
        assert(!localPwd.empty());
        return localPwd;
    }

private:

    void generateLocal(boost::mt19937& gen)
    {
        generatePrintableBytes(ICE_DEFAULT_UFRAG_LEN, gen, &_localUfrag);
        generatePrintableBytes(ICE_DEFAULT_PWD_LEN, gen, &_localPwd);
    }

    std::vector<sm_uint8_t> _localUfrag;
    std::vector<sm_uint8_t> _localPwd;
    std::vector<sm_uint8_t> _remoteUfrag;
    std::vector<sm_uint8_t> _remotePwd;
    std::vector<sm_uint8_t> _verifyingUname;
};

typedef boost::shared_ptr<IceCredentials> IceCredentialsPtr;

#endif
