#ifndef BITTORRENTCLIENT_TORRENTFILEPARSER_H
#define BITTORRENTCLIENT_TORRENTFILEPARSER_H
#include <string>
#include <vector>
#include <bencode/BDictionary.h>


class TorrentFileParser
{
private:
    bencoding::BDictionary* root;
public:
    explicit TorrentFileParser(const std::string& filePath);
    long getFileSize() const;
    long getPieceLength() const;
    std::string getFileName() const;
    std::string getAnnounce() const;
    bencoding::BItem* get(std::string key) const;
    std::string getInfoHash() const;
    std::vector<std::string> splitPieceHashes() const;
};


#endif //BITTORRENTCLIENT_TORRENTFILEPARSER_H