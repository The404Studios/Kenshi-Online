namespace KenshiMultiplayer
{
    public class PlayerData
    {
        public string PlayerId { get; set; }
        public float PositionX { get; set; }
        public float PositionY { get; set; }
        public Dictionary<string, int> Inventory { get; set; }
        public int Health { get; set; }
    }
}
