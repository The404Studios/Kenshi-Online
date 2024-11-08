using System.Collections.Generic;
using System.Net.Sockets;

namespace KenshiMultiplayer
{
    public class Lobby
    {
        public string LobbyId { get; private set; }
        public List<TcpClient> Players { get; } = new List<TcpClient>();
        public const int MaxPlayers = 10; // Set max players per lobby

        public Lobby(string lobbyId)
        {
            LobbyId = lobbyId;
        }

        public bool AddPlayer(TcpClient client)
        {
            if (Players.Count >= MaxPlayers)
            {
                return false; // Lobby is full
            }

            Players.Add(client);
            return true;
        }

        public void RemovePlayer(TcpClient client)
        {
            Players.Remove(client);
        }

        public bool IsEmpty() => Players.Count == 0;
    }
}
