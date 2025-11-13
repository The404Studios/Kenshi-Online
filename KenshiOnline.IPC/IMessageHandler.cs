using System.Threading.Tasks;

namespace KenshiOnline.IPC
{
    /// <summary>
    /// Interface for handling IPC messages
    /// </summary>
    public interface IMessageHandler
    {
        Task<IPCMessage> HandleMessageAsync(string clientId, IPCMessage message);
    }
}
