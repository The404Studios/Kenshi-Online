using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Linq;

namespace KenshiOnline.Core.Squad
{
    /// <summary>
    /// Squad member data
    /// </summary>
    public class SquadMember
    {
        public string PlayerId { get; set; }
        public string PlayerName { get; set; }
        public bool IsLeader { get; set; }
        public bool IsOnline { get; set; }
        public DateTime JoinedAt { get; set; }
        public Dictionary<string, object> Metadata { get; set; }

        public SquadMember()
        {
            JoinedAt = DateTime.UtcNow;
            IsOnline = true;
            Metadata = new Dictionary<string, object>();
        }

        public Dictionary<string, object> Serialize()
        {
            return new Dictionary<string, object>
            {
                ["playerId"] = PlayerId ?? "",
                ["playerName"] = PlayerName ?? "",
                ["isLeader"] = IsLeader,
                ["isOnline"] = IsOnline,
                ["joinedAt"] = JoinedAt.ToString("o"),
                ["metadata"] = Metadata
            };
        }
    }

    /// <summary>
    /// Squad data
    /// </summary>
    public class Squad
    {
        public string SquadId { get; set; }
        public string SquadName { get; set; }
        public string LeaderId { get; set; }
        public List<SquadMember> Members { get; set; }
        public int MaxMembers { get; set; }
        public DateTime CreatedAt { get; set; }
        public bool IsPublic { get; set; }
        public string Password { get; set; }
        public Dictionary<string, object> Settings { get; set; }

        public Squad()
        {
            SquadId = Guid.NewGuid().ToString();
            Members = new List<SquadMember>();
            MaxMembers = 8;
            CreatedAt = DateTime.UtcNow;
            IsPublic = true;
            Settings = new Dictionary<string, object>();
        }

        public int MemberCount => Members.Count;
        public int OnlineMemberCount => Members.Count(m => m.IsOnline);
        public bool IsFull => Members.Count >= MaxMembers;

        public Dictionary<string, object> Serialize()
        {
            return new Dictionary<string, object>
            {
                ["squadId"] = SquadId,
                ["squadName"] = SquadName ?? "",
                ["leaderId"] = LeaderId ?? "",
                ["members"] = Members.Select(m => m.Serialize()).ToList(),
                ["maxMembers"] = MaxMembers,
                ["memberCount"] = MemberCount,
                ["onlineMemberCount"] = OnlineMemberCount,
                ["createdAt"] = CreatedAt.ToString("o"),
                ["isPublic"] = IsPublic,
                ["hasPassword"] = !string.IsNullOrEmpty(Password),
                ["settings"] = Settings
            };
        }
    }

    /// <summary>
    /// Squad invitation
    /// </summary>
    public class SquadInvitation
    {
        public string InvitationId { get; set; }
        public string SquadId { get; set; }
        public string SquadName { get; set; }
        public string InviterId { get; set; }
        public string InviterName { get; set; }
        public string InviteeId { get; set; }
        public DateTime CreatedAt { get; set; }
        public DateTime ExpiresAt { get; set; }

        public SquadInvitation()
        {
            InvitationId = Guid.NewGuid().ToString();
            CreatedAt = DateTime.UtcNow;
            ExpiresAt = CreatedAt.AddMinutes(5); // 5 minute expiration
        }

        public bool IsExpired => DateTime.UtcNow > ExpiresAt;

        public Dictionary<string, object> Serialize()
        {
            return new Dictionary<string, object>
            {
                ["invitationId"] = InvitationId,
                ["squadId"] = SquadId,
                ["squadName"] = SquadName ?? "",
                ["inviterId"] = InviterId ?? "",
                ["inviterName"] = InviterName ?? "",
                ["inviteeId"] = InviteeId ?? "",
                ["createdAt"] = CreatedAt.ToString("o"),
                ["expiresAt"] = ExpiresAt.ToString("o"),
                ["isExpired"] = IsExpired
            };
        }
    }

    /// <summary>
    /// Squad system for managing player squads/parties
    /// </summary>
    public class SquadSystem
    {
        private readonly ConcurrentDictionary<string, Squad> _squads; // SquadId -> Squad
        private readonly ConcurrentDictionary<string, string> _playerSquads; // PlayerId -> SquadId
        private readonly ConcurrentDictionary<string, SquadInvitation> _invitations; // InvitationId -> Invitation
        private readonly object _lock = new object();

        // Events
        public event Action<Squad> OnSquadCreated;
        public event Action<Squad> OnSquadDisbanded;
        public event Action<Squad, SquadMember> OnMemberJoined;
        public event Action<Squad, SquadMember> OnMemberLeft;
        public event Action<Squad, string> OnLeaderChanged;
        public event Action<SquadInvitation> OnInvitationSent;

        // Statistics
        public int TotalSquads => _squads.Count;
        public int TotalMembers => _squads.Values.Sum(s => s.MemberCount);
        public int TotalInvitations => _invitations.Count;

        public SquadSystem()
        {
            _squads = new ConcurrentDictionary<string, Squad>();
            _playerSquads = new ConcurrentDictionary<string, string>();
            _invitations = new ConcurrentDictionary<string, SquadInvitation>();
        }

        #region Squad Management

        /// <summary>
        /// Create new squad
        /// </summary>
        public Squad CreateSquad(string leaderId, string leaderName, string squadName, bool isPublic = true, string password = null, int maxMembers = 8)
        {
            // Check if player is already in a squad
            if (_playerSquads.ContainsKey(leaderId))
                return null;

            var squad = new Squad
            {
                SquadName = squadName,
                LeaderId = leaderId,
                IsPublic = isPublic,
                Password = password,
                MaxMembers = maxMembers
            };

            // Add leader as first member
            var leaderMember = new SquadMember
            {
                PlayerId = leaderId,
                PlayerName = leaderName,
                IsLeader = true
            };

            squad.Members.Add(leaderMember);

            // Register squad
            _squads[squad.SquadId] = squad;
            _playerSquads[leaderId] = squad.SquadId;

            OnSquadCreated?.Invoke(squad);

            return squad;
        }

        /// <summary>
        /// Disband squad
        /// </summary>
        public bool DisbandSquad(string squadId, string playerId)
        {
            if (!_squads.TryGetValue(squadId, out var squad))
                return false;

            // Only leader can disband
            if (squad.LeaderId != playerId)
                return false;

            // Remove all members from player->squad mapping
            foreach (var member in squad.Members)
            {
                _playerSquads.TryRemove(member.PlayerId, out _);
            }

            // Remove squad
            _squads.TryRemove(squadId, out _);

            OnSquadDisbanded?.Invoke(squad);

            return true;
        }

        /// <summary>
        /// Get squad
        /// </summary>
        public Squad GetSquad(string squadId)
        {
            _squads.TryGetValue(squadId, out var squad);
            return squad;
        }

        /// <summary>
        /// Get player's squad
        /// </summary>
        public Squad GetPlayerSquad(string playerId)
        {
            if (_playerSquads.TryGetValue(playerId, out var squadId))
            {
                return GetSquad(squadId);
            }
            return null;
        }

        /// <summary>
        /// Get all squads
        /// </summary>
        public IEnumerable<Squad> GetAllSquads()
        {
            return _squads.Values;
        }

        /// <summary>
        /// Get public squads
        /// </summary>
        public IEnumerable<Squad> GetPublicSquads()
        {
            return _squads.Values.Where(s => s.IsPublic);
        }

        #endregion

        #region Member Management

        /// <summary>
        /// Invite player to squad
        /// </summary>
        public SquadInvitation InvitePlayer(string squadId, string inviterId, string inviterName, string inviteeId)
        {
            var squad = GetSquad(squadId);
            if (squad == null)
                return null;

            // Check if inviter is in squad
            if (!squad.Members.Any(m => m.PlayerId == inviterId))
                return null;

            // Check if invitee is already in a squad
            if (_playerSquads.ContainsKey(inviteeId))
                return null;

            // Check if squad is full
            if (squad.IsFull)
                return null;

            // Create invitation
            var invitation = new SquadInvitation
            {
                SquadId = squadId,
                SquadName = squad.SquadName,
                InviterId = inviterId,
                InviterName = inviterName,
                InviteeId = inviteeId
            };

            _invitations[invitation.InvitationId] = invitation;

            OnInvitationSent?.Invoke(invitation);

            return invitation;
        }

        /// <summary>
        /// Accept squad invitation
        /// </summary>
        public bool AcceptInvitation(string invitationId, string playerName)
        {
            if (!_invitations.TryGetValue(invitationId, out var invitation))
                return false;

            // Check if expired
            if (invitation.IsExpired)
            {
                _invitations.TryRemove(invitationId, out _);
                return false;
            }

            var squad = GetSquad(invitation.SquadId);
            if (squad == null)
                return false;

            // Check if squad is full
            if (squad.IsFull)
                return false;

            // Add player to squad
            var member = new SquadMember
            {
                PlayerId = invitation.InviteeId,
                PlayerName = playerName,
                IsLeader = false
            };

            squad.Members.Add(member);
            _playerSquads[invitation.InviteeId] = squad.SquadId;

            // Remove invitation
            _invitations.TryRemove(invitationId, out _);

            OnMemberJoined?.Invoke(squad, member);

            return true;
        }

        /// <summary>
        /// Decline squad invitation
        /// </summary>
        public bool DeclineInvitation(string invitationId)
        {
            return _invitations.TryRemove(invitationId, out _);
        }

        /// <summary>
        /// Leave squad
        /// </summary>
        public bool LeaveSquad(string playerId)
        {
            if (!_playerSquads.TryGetValue(playerId, out var squadId))
                return false;

            var squad = GetSquad(squadId);
            if (squad == null)
                return false;

            var member = squad.Members.FirstOrDefault(m => m.PlayerId == playerId);
            if (member == null)
                return false;

            // If leader is leaving, transfer leadership or disband
            if (member.IsLeader)
            {
                if (squad.Members.Count > 1)
                {
                    // Transfer leadership to next member
                    var newLeader = squad.Members.FirstOrDefault(m => !m.IsLeader);
                    if (newLeader != null)
                    {
                        newLeader.IsLeader = true;
                        squad.LeaderId = newLeader.PlayerId;
                        OnLeaderChanged?.Invoke(squad, newLeader.PlayerId);
                    }
                }
                else
                {
                    // Last member, disband squad
                    return DisbandSquad(squadId, playerId);
                }
            }

            // Remove member
            squad.Members.Remove(member);
            _playerSquads.TryRemove(playerId, out _);

            OnMemberLeft?.Invoke(squad, member);

            return true;
        }

        /// <summary>
        /// Kick member from squad
        /// </summary>
        public bool KickMember(string squadId, string kickerId, string targetId)
        {
            var squad = GetSquad(squadId);
            if (squad == null)
                return false;

            // Only leader can kick
            if (squad.LeaderId != kickerId)
                return false;

            // Can't kick leader
            if (targetId == kickerId)
                return false;

            var member = squad.Members.FirstOrDefault(m => m.PlayerId == targetId);
            if (member == null)
                return false;

            // Remove member
            squad.Members.Remove(member);
            _playerSquads.TryRemove(targetId, out _);

            OnMemberLeft?.Invoke(squad, member);

            return true;
        }

        /// <summary>
        /// Transfer leadership
        /// </summary>
        public bool TransferLeadership(string squadId, string currentLeaderId, string newLeaderId)
        {
            var squad = GetSquad(squadId);
            if (squad == null)
                return false;

            // Check if current player is leader
            if (squad.LeaderId != currentLeaderId)
                return false;

            // Check if new leader is in squad
            var newLeader = squad.Members.FirstOrDefault(m => m.PlayerId == newLeaderId);
            if (newLeader == null)
                return false;

            // Transfer leadership
            var oldLeader = squad.Members.FirstOrDefault(m => m.PlayerId == currentLeaderId);
            if (oldLeader != null)
            {
                oldLeader.IsLeader = false;
            }

            newLeader.IsLeader = true;
            squad.LeaderId = newLeaderId;

            OnLeaderChanged?.Invoke(squad, newLeaderId);

            return true;
        }

        #endregion

        #region Invitations

        /// <summary>
        /// Get player's pending invitations
        /// </summary>
        public IEnumerable<SquadInvitation> GetPlayerInvitations(string playerId)
        {
            return _invitations.Values.Where(i => i.InviteeId == playerId && !i.IsExpired);
        }

        #endregion

        #region Maintenance

        /// <summary>
        /// Update squad system (cleanup expired invitations)
        /// </summary>
        public void Update()
        {
            // Remove expired invitations
            var expired = _invitations.Values.Where(i => i.IsExpired).Select(i => i.InvitationId).ToList();
            foreach (var id in expired)
            {
                _invitations.TryRemove(id, out _);
            }
        }

        /// <summary>
        /// Set member online status
        /// </summary>
        public void SetMemberOnlineStatus(string playerId, bool isOnline)
        {
            var squad = GetPlayerSquad(playerId);
            if (squad != null)
            {
                var member = squad.Members.FirstOrDefault(m => m.PlayerId == playerId);
                if (member != null)
                {
                    member.IsOnline = isOnline;
                }
            }
        }

        #endregion

        #region Statistics

        /// <summary>
        /// Get squad statistics
        /// </summary>
        public Dictionary<string, object> GetStatistics()
        {
            return new Dictionary<string, object>
            {
                ["totalSquads"] = TotalSquads,
                ["totalMembers"] = TotalMembers,
                ["totalInvitations"] = TotalInvitations,
                ["averageMembersPerSquad"] = TotalSquads > 0 ? (float)TotalMembers / TotalSquads : 0,
                ["publicSquads"] = GetPublicSquads().Count()
            };
        }

        #endregion
    }
}
