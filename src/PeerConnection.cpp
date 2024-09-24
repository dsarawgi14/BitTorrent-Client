#include <stdexcept>
#include <iostream>
#include <cassert>
#include <cstring>
#include <chrono>
#include <unistd.h>
#include <netinet/in.h>
#include <loguru/loguru.hpp>
#include <utility>

#include "PeerConnection.h"
#include "utils.h"
#include "connect.h"

#define INFO_HASH_STARTING_POS 28
#define PEER_ID_STARTING_POS 48
#define HASH_LEN 20
#define DUMMY_PEER_IP "0.0.0.0"

/**
 * Constructor of the class PeerConnection.
 * @param queue: the thread-safe queue that contains the available peers.
 * @param clientId: the peer ID of this C++ BitTorrent client. Generated in the TorrentClient class.
 * @param infoHash: info hash of the Torrent file.
 * @param pieceManager: pointer to the PieceManager.
 */
PeerConnection::PeerConnection(
    SharedQueue<Peer*>* queue,
    std::string clientId,
    std::string infoHash,
    PieceManager* pieceManager
) : queue(queue), clientId(std::move(clientId)), infoHash(std::move(infoHash)), pieceManager(pieceManager) {}


/**
 * Destructor of the PeerConnection class. Closes the established TCP connection with the peer
 * on object destruction.
 */
PeerConnection::~PeerConnection() {
    closeSock();
    LOG_F(INFO, "Downloading thread terminated");
}


void PeerConnection::start() {
    LOG_F(INFO, "Downloading thread started...");
    while (!(terminated || pieceManager->isComplete()))
    {
        peer = queue->pop_front();
        // Terminates the thread if it has received a dummy Peer
        if (peer->ip == DUMMY_PEER_IP)
            return;

        try
        {
            // Establishes connection with the peer, and lets it know
            // that we are interested.
            if (establishNewConnection())
            {
                while (!pieceManager->isComplete())
                {
                    BitTorrentMessage message = receiveMessage();
                    if (message.getMessageId() > 10)
                        throw std::runtime_error("Received invalid message Id from peer " + peerId);
                    switch (message.getMessageId())
                    {
                        case choke:
                            choked = true;
                            break;

                        case unchoke:
                            choked = false;
                            break;

                        case piece:
                        {
                            requestPending = false;
                            std::string payload = message.getPayload();
                            int index = bytesToInt(payload.substr(0, 4));
                            int begin = bytesToInt(payload.substr(4, 4));
                            std::string blockData = payload.substr(8);
                            pieceManager->blockReceived(peerId, index, begin, blockData);
                            break;
                        }
                        case have:
                        {
                            std::string payload = message.getPayload();
                            int pieceIndex = bytesToInt(payload);
                            pieceManager->updatePeer(peerId, pieceIndex);
                            break;
                        }

                        default:
                            break;
                    }
                    if (!choked)
                    {
                        if (!requestPending)
                        {
                            requestPiece();
                        }
                    }
                }
            }
        }
        catch (std::exception &e)
        {
            closeSock();
            LOG_F(ERROR, "An error occurred while downloading from peer %s [%s]", peerId.c_str(), peer->ip.c_str());
            LOG_F(ERROR, "%s", e.what());
        }
    }
}

/**
 * Terminates the peer connection
 */
void PeerConnection::stop()
{
    terminated = true;
}

