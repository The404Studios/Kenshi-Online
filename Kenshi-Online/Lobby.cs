﻿using System.Collections.Generic;
using System.Net.Sockets;
using System.Text;

namespace KenshiMultiplayer
{
    public class Lobby
    {
        public string LobbyId { get; private set; }
        public List<TcpClient> Players { get; } = new List<TcpClient>();
        public bool IsPrivate { get; private set; }
        public string Password { get; private set; }
        public int MaxPlayers { get; private set; } = 10;
        public Dictionary<string, TcpClient> PlayerConnections { get; private set; } = new Dictionary<string, TcpClient>();

        public Lobby(string lobbyId, bool isPrivate = false, string password = "", int maxPlayers = 10)
        {
            LobbyId = lobbyId;
            IsPrivate = isPrivate;
            Password = password;
            MaxPlayers = maxPlayers;
        }

        public Dictionary<string, List<TcpClient>> ChatChannels { get; private set; } = new Dictionary<string, List<TcpClient>>
    {
        { "general", new List<TcpClient>() },
        { "trade", new List<TcpClient>() },
        { "combat", new List<TcpClient>() }
    };

        public void JoinChannel(string channel, TcpClient client)
        {
            if (ChatChannels.ContainsKey(channel))
            {
                ChatChannels[channel].Add(client);
            }
        }

        public void BroadcastToChannel(string channel, string message, TcpClient senderClient)
        {
            if (ChatChannels.TryGetValue(channel, out var clients))
            {
                foreach (var client in clients)
                {
                    if (client != senderClient && client.Connected)
                    {
                        NetworkStream stream = client.GetStream();
                        byte[] messageBuffer = Encoding.ASCII.GetBytes(message);
                        stream.Write(messageBuffer, 0, messageBuffer.Length);
                    }
                }
            }
        }

    public bool CanJoin(string password = "")
        {
            if (IsPrivate && Password != password) return false;
            return Players.Count < MaxPlayers;
        }

        public void AddPlayer(TcpClient client)
        {
            if (Players.Count < MaxPlayers)
            {
                Players.Add(client);
            }
        }

        public void RemovePlayer(TcpClient client)
        {
            Players.Remove(client);
        }

        public void ReconnectPlayer(string playerId, TcpClient client)
        {
            if (PlayerConnections.ContainsKey(playerId))
            {
                PlayerConnections[playerId] = client;
            }
            else
            {
                PlayerConnections.Add(playerId, client);
                AddPlayer(client);
            }
        }


        public void SetMaxPlayers(int maxPlayers)
        {
            MaxPlayers = maxPlayers;
        }

        public void BroadcastChatMessage(string message, TcpClient senderClient)
        {
            foreach (var client in Players)
            {
                if (client != senderClient && client.Connected)
                {
                    NetworkStream stream = client.GetStream();
                    byte[] messageBuffer = Encoding.ASCII.GetBytes(message);
                    stream.Write(messageBuffer, 0, messageBuffer.Length);
                }
            }
        }

        public void KickPlayer(string playerId)
        {
            if (PlayerConnections.TryGetValue(playerId, out var client))
            {
                RemovePlayer(client);
                client.Close();
            }
        }
    }
}
