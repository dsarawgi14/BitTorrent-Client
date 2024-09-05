#include <string>
#include <iostream>
#include <cpr/cpr.h>
#include <random>
#include <stdexcept>
#include <bitset>
#include <bencode/bencoding.h>
#include <loguru/loguru.hpp>
#include <utility>

#include "utils.h"
#include "PeerRetriever.h"

#define TRACKER_TIMEOUT 15000

/**
 * Constructor of the class PeerRetriever. Takes in the URL as specified by the
 * value of announce in the Torrent file, the info hash of the file, as well as
 * a port number.
 * @param announceURL: the HTTP URL to the tracker.
 * @param infoHash: the info hash of the Torrent file.
 * @param port: the TCP port this client listens on.
 * @param fileSize: the size of the file to be downloaded in bytes.
 */
PeerRetriever::PeerRetriever(
        std::string peerId,
        std::string announceUrl,
        std::string infoHash,
        int port,
        const unsigned long fileSize
): fileSize(fileSize)
{
    this->peerId = std::move(peerId);
    this->announceUrl = std::move(announceUrl);
    this->infoHash = std::move(infoHash);
    this->port = port;
}

/**
 * Retrieves the list of peers from the URL specified by the 'announce' property.
 * The list of parameters and their descriptions are as follows
 * - info_hash: the SHA1 hash of the info dict found in the .torrent.
 * - peer_id: a unique ID generated for this client.
 * - uploaded: the total number of bytes uploaded.
 * - downloaded: the total number of bytes downloaded.
 * - left: the number of bytes left to download for this client.
 * - port: the TCP port this client listens on.
 * - compact: whether or not the client accepts a compacted list of peers or not.
 * @return a vector that contains the information of all peers.
 */
std::vector<Peer*> PeerRetriever::retrievePeers(unsigned long bytesDownloaded)
{
    std::stringstream info;
    info << "Retrieving peers from " << announceUrl << " with the following parameters..." << std::endl;
    // Note that info hash will be URL-encoded by the cpr library
    info << "info_hash: " << infoHash << std::endl;
    info << "peer_id: " << peerId << std::endl;
    info << "port: " << port << std::endl;
    info << "uploaded: " << 0 << std::endl;
    info << "downloaded: " << std::to_string(bytesDownloaded) << std::endl;
    info << "left: " << std::to_string(fileSize - bytesDownloaded) << std::endl;
    info << "compact: " << std::to_string(1);

    LOG_F(INFO, "%s", info.str().c_str());

    cpr::Response res = cpr::Get(cpr::Url{announceUrl}, cpr::Parameters {
            { "info_hash", std::string(hexDecode(infoHash)) },
            { "peer_id", std::string(peerId) },
            { "port", std::to_string(port) },
            { "uploaded", std::to_string(0) },
            { "downloaded", std::to_string(bytesDownloaded) },
            { "left", std::to_string(fileSize - bytesDownloaded) },
            { "compact", std::to_string(1) }
        }, cpr::Timeout{ TRACKER_TIMEOUT }
    );

    if (res.status_code == 200)
    {
        LOG_F(INFO, "Retrieve response from tracker: SUCCESS");
        std::vector<Peer*> peers = decodeResponse(res.text);
        return peers;
    }
    else
    {
        LOG_F(ERROR, "Retrieving response from tracker: FAILED [ %d: %s ]", res.status_code, res.text.c_str());
    }
    return std::vector<Peer*>();
}


