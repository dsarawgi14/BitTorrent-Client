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


/**
 * Establishes a TCP connection with the peer and sent it our initial BitTorrent handshake message.
 * Waits for its reply, and compares the info hash contained in its response message with
 * the info hash we calculated from the Torrent file. If they do not match, close the connection.
 */
void PeerConnection::performHandshake()
{
    // Connects to the peer
    LOG_F(INFO, "Connecting to peer [%s]...", peer->ip.c_str());
    try
    {
        sock = createConnection(peer->ip, peer->port);
    }
    catch (std::runtime_error &e)
    {
        throw std::runtime_error("Cannot connect to peer [" + peer->ip + "]");
    }
    LOG_F(INFO, "Establish TCP connection with peer at socket %d: SUCCESS", sock);

    // Send the handshake message to the peer
    LOG_F(INFO, "Sending handshake message to [%s]...", peer->ip.c_str());
    std::string handshakeMessage = createHandshakeMessage();
    sendData(sock, handshakeMessage);
    LOG_F(INFO, "Send handshake message: SUCCESS");

    // Receive the reply from the peer
    LOG_F(INFO, "Receiving handshake reply from peer [%s]...", peer->ip.c_str());
    std::string reply = receiveData(sock, handshakeMessage.length());
    if (reply.empty())
        throw std::runtime_error("Receive handshake from peer: FAILED [No response from peer]");
    peerId = reply.substr(PEER_ID_STARTING_POS, HASH_LEN);
    LOG_F(INFO, "Receive handshake reply from peer: SUCCESS");

    // Compare the info hash from the peer's reply message with the info hash we sent.
    // If the two values are not the same, close the connection and raise an exception.
    std::string receivedInfoHash = reply.substr(INFO_HASH_STARTING_POS, HASH_LEN);
    if ((receivedInfoHash == infoHash) != 0)
        throw std::runtime_error("Perform handshake with peer " + peer->ip +
                                 ": FAILED [Received mismatching info hash]");
    LOG_F(INFO, "Hash comparison: SUCCESS");
}

