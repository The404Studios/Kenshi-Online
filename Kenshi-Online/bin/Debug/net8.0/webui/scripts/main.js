// Main JavaScript for Kenshi Online Web Interface
document.addEventListener('DOMContentLoaded', function() {
    // State management
    const state = {
        isLoggedIn: false,
        username: null,
        token: null,
        activeTab: 'status',
        inventoryItems: [],
        friendsList: [],
        marketplaceListings: [],
        currentTrade: null
    };

    // DOM elements
    const elements = {
        userStatus: document.getElementById('status-message'),
        loginBtn: document.getElementById('login-btn'),
        logoutBtn: document.getElementById('logout-btn'),
        loginForm: document.getElementById('login-form'),
        registerForm: document.getElementById('register-form'),
        dashboard: document.getElementById('dashboard'),
        registerLink: document.getElementById('register-link'),
        loginLink: document.getElementById('login-link'),
        tabButtons: document.querySelectorAll('.tab-btn'),
        tabContents: document.querySelectorAll('.tab-content')
    };

    // Initialize the app
    init();

    // Initialize the application
    function init() {
        // Set up event listeners
        setupEventListeners();
        
        // Check login status
        checkLoginStatus();
        
        // Periodically refresh status
        setInterval(checkLoginStatus, 10000);
    }

    // Set up all event listeners
    function setupEventListeners() {
        // Login/Logout buttons
        elements.loginBtn.addEventListener('click', () => {
            showLoginForm();
        });
        
        elements.logoutBtn.addEventListener('click', () => {
            logout();
        });
        
        // Register/Login form links
        elements.registerLink.addEventListener('click', () => {
            hideLoginForm();
            showRegisterForm();
        });
        
        elements.loginLink.addEventListener('click', () => {
            hideRegisterForm();
            showLoginForm();
        });
        
        // Login form submission
        document.getElementById('form-login').addEventListener('submit', function(e) {
            e.preventDefault();
            login();
        });
        
        // Register form submission
        document.getElementById('form-register').addEventListener('submit', function(e) {
            e.preventDefault();
            register();
        });
        
        // Tab switching
        elements.tabButtons.forEach(button => {
            button.addEventListener('click', () => {
                const tabName = button.getAttribute('data-tab');
                switchTab(tabName);
            });
        });
        
        // Add Friend form
        document.getElementById('form-add-friend').addEventListener('submit', function(e) {
            e.preventDefault();
            addFriend();
        });
        
        // Create Listing form
        document.getElementById('form-create-listing').addEventListener('submit', function(e) {
            e.preventDefault();
            createListing();
        });
        
        // Initiate Trade form
        document.getElementById('form-initiate-trade').addEventListener('submit', function(e) {
            e.preventDefault();
            initiateTrade();
        });
        
        // Condition slider
        const conditionSlider = document.getElementById('listing-condition');
        const conditionValue = document.getElementById('condition-value');
        
        if (conditionSlider && conditionValue) {
            conditionSlider.addEventListener('input', function() {
                const value = Math.round(this.value * 100);
                conditionValue.textContent = `${value}%`;
            });
        }
    }

    // Check if user is logged in
    function checkLoginStatus() {
        fetch('/api/status')
            .then(response => response.json())
            .then(data => {
                if (data.success && data.loggedIn) {
                    state.isLoggedIn = true;
                    state.username = data.username;
                    
                    // Update UI for logged-in state
                    elements.userStatus.textContent = `Logged in as ${state.username}`;
                    elements.loginBtn.style.display = 'none';
                    elements.logoutBtn.style.display = 'inline-block';
                    
                    hideLoginForm();
                    hideRegisterForm();
                    showDashboard();
                    
                    // Load dashboard data
                    loadDashboardData();
                } else {
                    state.isLoggedIn = false;
                    state.username = null;
                    
                    // Update UI for logged-out state
                    elements.userStatus.textContent = 'Not logged in';
                    elements.loginBtn.style.display = 'inline-block';
                    elements.logoutBtn.style.display = 'none';
                    
                    hideDashboard();
                    showLoginForm();
                }
            })
            .catch(error => {
                console.error('Error checking login status:', error);
                showNotification('Error', 'Failed to connect to server', 'error');
            });
    }

    // Login functionality
    function login() {
        const username = document.getElementById('username').value;
        const password = document.getElementById('password').value;
        
        if (!username || !password) {
            showNotification('Error', 'Username and password are required', 'error');
            return;
        }
        
        fetch('/api/login', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({
                username,
                password
            })
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                state.isLoggedIn = true;
                state.username = data.username;
                state.token = data.token;
                
                // Store token in localStorage for persistence
                localStorage.setItem('kenshiToken', data.token);
                
                showNotification('Success', 'Login successful!', 'success');
                checkLoginStatus();
            } else {
                showNotification('Error', data.error || 'Login failed', 'error');
            }
        })
        .catch(error => {
            console.error('Error during login:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    }

    // Logout functionality
    function logout() {
        // Clear token from localStorage
        localStorage.removeItem('kenshiToken');
        
        // Call logout API
        fetch('/api/logout', {
            method: 'POST',
            headers: {
                'Authorization': `Bearer ${state.token}`
            }
        })
        .then(response => response.json())
        .then(data => {
            state.isLoggedIn = false;
            state.username = null;
            state.token = null;
            
            showNotification('Success', 'Logout successful', 'success');
            checkLoginStatus();
        })
        .catch(error => {
            console.error('Error during logout:', error);
            
            // Even if API fails, we still want to log out client-side
            state.isLoggedIn = false;
            state.username = null;
            state.token = null;
            
            checkLoginStatus();
        });
    }

    // Register functionality
    function register() {
        const username = document.getElementById('reg-username').value;
        const password = document.getElementById('reg-password').value;
        const email = document.getElementById('reg-email').value;
        
        if (!username || !password || !email) {
            showNotification('Error', 'All fields are required', 'error');
            return;
        }
        
        // Validate username length
        if (username.length < 3) {
            showNotification('Error', 'Username must be at least 3 characters', 'error');
            return;
        }
        
        // Validate password length
        if (password.length < 8) {
            showNotification('Error', 'Password must be at least 8 characters', 'error');
            return;
        }
        
        // Validate email format
        const emailRegex = /^[^\s@]+@[^\s@]+\.[^\s@]+$/;
        if (!emailRegex.test(email)) {
            showNotification('Error', 'Invalid email format', 'error');
            return;
        }
        
        fetch('/api/register', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({
                username,
                password,
                email
            })
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Success', 'Registration successful! You can now log in.', 'success');
                hideRegisterForm();
                showLoginForm();
            } else {
                showNotification('Error', data.error || 'Registration failed', 'error');
            }
        })
        .catch(error => {
            console.error('Error during registration:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    }

    // Switch tabs in the dashboard
    function switchTab(tabName) {
        state.activeTab = tabName;
        
        // Update tab buttons
        elements.tabButtons.forEach(button => {
            if (button.getAttribute('data-tab') === tabName) {
                button.classList.add('active');
            } else {
                button.classList.remove('active');
            }
        });
        
        // Update tab contents
        elements.tabContents.forEach(content => {
            if (content.id === `tab-${tabName}`) {
                content.classList.add('active');
            } else {
                content.classList.remove('active');
            }
        });
        
        // Load tab-specific data
        loadTabData(tabName);
    }

    // Load data for the active tab
    function loadTabData(tabName) {
        switch (tabName) {
            case 'status':
                loadStatusData();
                break;
            case 'friends':
                loadFriendsData();
                break;
            case 'marketplace':
                loadMarketplaceData();
                break;
            case 'trade':
                loadTradeData();
                break;
            case 'inventory':
                loadInventoryData();
                break;
            case 'mods':
                loadModsData();
                break;
        }
    }

    // Load all dashboard data
    function loadDashboardData() {
        // Load data for the active tab
        loadTabData(state.activeTab);
    }

    // Load player and server status data
    function loadStatusData() {
        const playerStatusElement = document.getElementById('player-status');
        const serverStatusElement = document.getElementById('server-status');
        
        // Load player status
        fetch('/api/player/status', {
            headers: {
                'Authorization': `Bearer ${state.token}`
            }
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                const player = data.player;
                let html = `
                    <div class="status-grid">
                        <div class="status-item">
                            <span class="status-label">Name:</span>
                            <span class="status-value">${player.displayName}</span>
                        </div>
                        <div class="status-item">
                            <span class="status-label">Health:</span>
                            <span class="status-value">${player.health}/${player.maxHealth}</span>
                        </div>
                        <div class="status-item">
                            <span class="status-label">Level:</span>
                            <span class="status-value">${player.level}</span>
                        </div>
                        <div class="status-item">
                            <span class="status-label">Hunger:</span>
                            <span class="status-value">${player.hunger}%</span>
                        </div>
                        <div class="status-item">
                            <span class="status-label">Thirst:</span>
                            <span class="status-value">${player.thirst}%</span>
                        </div>
                    </div>
                `;
                playerStatusElement.innerHTML = html;
            } else {
                playerStatusElement.innerHTML = `<p class="error">${data.error || 'Failed to load player status'}</p>`;
            }
        })
        .catch(error => {
            console.error('Error loading player status:', error);
            playerStatusElement.innerHTML = `<p class="error">Failed to connect to server</p>`;
        });
        
        // Load server status
        fetch('/api/game/status')
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                let html = `
                    <div class="status-grid">
                        <div class="status-item">
                            <span class="status-label">Server Address:</span>
                            <span class="status-value">${data.serverAddress}:${data.serverPort}</span>
                        </div>
                        <div class="status-item">
                            <span class="status-label">Web Interface:</span>
                            <span class="status-value">${data.webInterfaceEnabled ? 'Enabled' : 'Disabled'}</span>
                        </div>
                        <div class="status-item">
                            <span class="status-label">WebUI Version:</span>
                            <span class="status-value">${data.webUiVersion}</span>
                        </div>
                    </div>
                `;
                serverStatusElement.innerHTML = html;
            } else {
                serverStatusElement.innerHTML = `<p class="error">${data.error || 'Failed to load server status'}</p>`;
            }
        })
        .catch(error => {
            console.error('Error loading server status:', error);
            serverStatusElement.innerHTML = `<p class="error">Failed to connect to server</p>`;
        });
    }

    // Load friends data
    function loadFriendsData() {
        const friendsListElement = document.getElementById('friends-list');
        const friendRequestsElement = document.getElementById('friend-requests');
        
        // Show loading state
        friendsListElement.innerHTML = '<p>Loading friends...</p>';
        friendRequestsElement.innerHTML = '<p>Loading friend requests...</p>';
        
        fetch('/api/friends/list', {
            headers: {
                'Authorization': `Bearer ${state.token}`
            }
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                // Update state
                state.friendsList = data.friends;
                
                // Render friends list
                if (data.friends.length === 0) {
                    friendsListElement.innerHTML = '<p>You have no friends yet.</p>';
                } else {
                    let html = '';
                    data.friends.forEach(friend => {
                        const status = friend.isOnline ? 
                            '<span class="status online">Online</span>' : 
                            '<span class="status offline">Offline</span>';
                        
                        const lastSeen = friend.lastSeen ? 
                            new Date(friend.lastSeen).toLocaleString() : 
                            'Never';
                        
                        html += `
                            <div class="list-item">
                                <div>
                                    <strong>${friend.username}</strong>
                                    ${status}
                                </div>
                                <div class="actions">
                                    <button onclick="initiateTrade('${friend.username}')">Trade</button>
                                    <button class="secondary" onclick="removeFriend('${friend.username}')">Remove</button>
                                </div>
                            </div>
                        `;
                    });
                    friendsListElement.innerHTML = html;
                }
                
                // Render friend requests
                let incomingHtml = '';
                let outgoingHtml = '';
                
                if (data.incomingRequests.length === 0) {
                    incomingHtml = '<p>No incoming friend requests.</p>';
                } else {
                    data.incomingRequests.forEach(request => {
                        incomingHtml += `
                            <div class="list-item">
                                <div>
                                    <strong>${request}</strong>
                                </div>
                                <div class="actions">
                                    <button onclick="acceptFriendRequest('${request}')">Accept</button>
                                    <button class="secondary" onclick="declineFriendRequest('${request}')">Decline</button>
                                </div>
                            </div>
                        `;
                    });
                }
                
                if (data.outgoingRequests.length === 0) {
                    outgoingHtml = '<p>No outgoing friend requests.</p>';
                } else {
                    data.outgoingRequests.forEach(request => {
                        outgoingHtml += `
                            <div class="list-item">
                                <div>
                                    <strong>${request}</strong>
                                    <span class="status">Pending</span>
                                </div>
                                <div class="actions">
                                    <button class="secondary" onclick="cancelFriendRequest('${request}')">Cancel</button>
                                </div>
                            </div>
                        `;
                    });
                }
                
                friendRequestsElement.innerHTML = `
                    <h3>Incoming Requests</h3>
                    ${incomingHtml}
                    <h3>Outgoing Requests</h3>
                    ${outgoingHtml}
                `;
                
                // Update trade dropdown with friends
                updateTradeDropdown(data.friends);
            } else {
                friendsListElement.innerHTML = `<p class="error">${data.error || 'Failed to load friends'}</p>`;
                friendRequestsElement.innerHTML = `<p class="error">${data.error || 'Failed to load friend requests'}</p>`;
            }
        })
        .catch(error => {
            console.error('Error loading friends data:', error);
            friendsListElement.innerHTML = `<p class="error">Failed to connect to server</p>`;
            friendRequestsElement.innerHTML = `<p class="error">Failed to connect to server</p>`;
        });
    }

    // Load marketplace data
    function loadMarketplaceData() {
        const marketplaceListingsElement = document.getElementById('marketplace-listings');
        const myListingsElement = document.getElementById('my-listings');
        
        // Show loading state
        marketplaceListingsElement.innerHTML = '<p>Loading marketplace listings...</p>';
        myListingsElement.innerHTML = '<p>Loading your listings...</p>';
        
        fetch('/api/marketplace/listings', {
            headers: {
                'Authorization': `Bearer ${state.token}`
            }
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                // Update state
                state.marketplaceListings = data.activeListings;
                
                // Render marketplace listings
                if (data.activeListings.length === 0) {
                    marketplaceListingsElement.innerHTML = '<p>No items available for purchase.</p>';
                } else {
                    let html = '';
                    data.activeListings.forEach(listing => {
                        const condition = Math.round(listing.itemCondition * 100);
                        
                        html += `
                            <div class="list-item">
                                <div>
                                    <strong>${listing.itemName}</strong> x${listing.quantity}
                                    <span class="status">${condition}% Condition</span>
                                </div>
                                <div>
                                    <span>${listing.price} cats each (${listing.price * listing.quantity} total)</span>
                                    <div class="actions">
                                        <button onclick="purchaseListing('${listing.id}')">Purchase</button>
                                    </div>
                                </div>
                            </div>
                        `;
                    });
                    marketplaceListingsElement.innerHTML = html;
                }
                
                // Render my listings
                if (data.myListings.length === 0) {
                    myListingsElement.innerHTML = '<p>You have no active listings.</p>';
                } else {
                    let html = '';
                    data.myListings.forEach(listing => {
                        const condition = Math.round(listing.itemCondition * 100);
                        const listedDate = new Date(listing.listedAt).toLocaleString();
                        
                        html += `
                            <div class="list-item">
                                <div>
                                    <strong>${listing.itemName}</strong> x${listing.quantity}
                                    <span class="status">${condition}% Condition</span>
                                </div>
                                <div>
                                    <span>${listing.price} cats each (${listing.price * listing.quantity} total)</span>
                                    <small>Listed: ${listedDate}</small>
                                    <div class="actions">
                                        <button class="secondary" onclick="cancelListing('${listing.id}')">Cancel</button>
                                    </div>
                                </div>
                            </div>
                        `;
                    });
                    myListingsElement.innerHTML = html;
                }
                
                // Update item dropdown for creating listings
                loadInventoryForListings();
            } else {
                marketplaceListingsElement.innerHTML = `<p class="error">${data.error || 'Failed to load marketplace listings'}</p>`;
                myListingsElement.innerHTML = `<p class="error">${data.error || 'Failed to load your listings'}</p>`;
            }
        })
        .catch(error => {
            console.error('Error loading marketplace data:', error);
            marketplaceListingsElement.innerHTML = `<p class="error">Failed to connect to server</p>`;
            myListingsElement.innerHTML = `<p class="error">Failed to connect to server</p>`;
        });
    }

    // Load trade data
    function loadTradeData() {
        const currentTradeElement = document.getElementById('current-trade');
        const tradeRequestsElement = document.getElementById('trade-requests');
        
        // Show loading state
        currentTradeElement.innerHTML = '<p>Loading current trade...</p>';
        tradeRequestsElement.innerHTML = '<p>Loading trade requests...</p>';
        
        // Get current trade
        fetch('/api/trade/items', {
            headers: {
                'Authorization': `Bearer ${state.token}`
            }
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                if (data.trade) {
                    state.currentTrade = data.trade;
                    
                    // Determine trade partner
                    const isInitiator = data.trade.initiatorId === state.username;
                    const partner = isInitiator ? data.trade.targetId : data.trade.initiatorId;
                    
                    // Get offers
                    const yourOffer = isInitiator ? data.trade.initiatorOffer : data.trade.targetOffer;
                    const theirOffer = isInitiator ? data.trade.targetOffer : data.trade.initiatorOffer;
                    
                    // Render trade UI
                    let html = `
                        <div class="trade-container">
                            <h3>Trading with ${partner}</h3>
                            <div class="trade-offers">
                                <div class="trade-offer">
                                    <h4>Your Offer</h4>
                                    ${renderTradeItems(yourOffer.items, true)}
                                    <div class="trade-actions">
                                        <button id="add-item-btn">Add Item</button>
                                        <button class="${yourOffer.isConfirmed ? 'secondary' : ''}" 
                                                onclick="confirmTradeOffer()" 
                                                ${yourOffer.isConfirmed ? 'disabled' : ''}>
                                            ${yourOffer.isConfirmed ? 'Confirmed' : 'Confirm Offer'}
                                        </button>
                                    </div>
                                </div>
                                <div class="trade-offer">
                                    <h4>${partner}'s Offer</h4>
                                    ${renderTradeItems(theirOffer.items, false)}
                                    <div class="trade-status">
                                        ${theirOffer.isConfirmed ? 
                                            '<span class="status success">Offer Confirmed</span>' : 
                                            '<span class="status">Waiting for confirmation</span>'}
                                    </div>
                                </div>
                            </div>
                            <div class="trade-footer">
                                <button class="secondary" onclick="cancelTrade()">Cancel Trade</button>
                            </div>
                        </div>
                    `;
                    currentTradeElement.innerHTML = html;
                    
                    // Add event listener for the add item button
                    const addItemBtn = document.getElementById('add-item-btn');
                    if (addItemBtn) {
                        addItemBtn.addEventListener('click', () => {
                            showAddItemToTradeDialog();
                        });
                    }
                } else {
                    currentTradeElement.innerHTML = '<p>No active trade</p>';
                }
            } else {
                currentTradeElement.innerHTML = `<p class="error">${data.error || 'Failed to load current trade'}</p>`;
            }
        })
        .catch(error => {
            console.error('Error loading current trade:', error);
            currentTradeElement.innerHTML = `<p class="error">Failed to connect to server</p>`;
        });
        
        // Get trade requests
        // Implementation would be similar to above
    }

    // Helper function to render trade items
    function renderTradeItems(items, isYourItems) {
        if (!items || items.length === 0) {
            return '<p>No items added yet</p>';
        }
        
        let html = '<div class="trade-items">';
        items.forEach(item => {
            const condition = Math.round(item.condition * 100);
            
            html += `
                <div class="trade-item">
                    <div class="item-info">
                        <strong>${item.itemName}</strong> x${item.quantity}
                        <span class="status">${condition}% Condition</span>
                    </div>
                    ${isYourItems ? 
                        `<button class="secondary small" onclick="removeTradeItem('${item.itemId}')">Remove</button>` : 
                        ''}
                </div>
            `;
        });
        html += '</div>';
        
        return html;
    }

    // Load inventory data
    function loadInventoryData() {
        const inventoryItemsElement = document.getElementById('inventory-items');
        
        // Show loading state
        inventoryItemsElement.innerHTML = '<p>Loading inventory...</p>';
        
        fetch('/api/player/inventory', {
            headers: {
                'Authorization': `Bearer ${state.token}`
            }
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                // Update state
                state.inventoryItems = data.items;
                
                // Render inventory
                if (data.items.length === 0) {
                    inventoryItemsElement.innerHTML = '<p>Your inventory is empty.</p>';
                } else {
                    let html = '';
                    data.items.forEach(item => {
                        const condition = Math.round(item.condition * 100);
                        
                        html += `
                            <div class="list-item">
                                <div>
                                    <strong>${item.itemName}</strong> x${item.quantity}
                                    <span class="status">${condition}% Condition</span>
                                </div>
                                <div class="actions">
                                    <button onclick="sellItem('${item.itemId}', '${item.itemName}')">Sell</button>
                                </div>
                            </div>
                        `;
                    });
                    inventoryItemsElement.innerHTML = html;
                }
            } else {
                inventoryItemsElement.innerHTML = `<p class="error">${data.error || 'Failed to load inventory'}</p>`;
            }
        })
        .catch(error => {
            console.error('Error loading inventory data:', error);
            inventoryItemsElement.innerHTML = `<p class="error">Failed to connect to server</p>`;
        });
    }

    // Load mods data
    function loadModsData() {
        const modsElement = document.getElementById('installed-mods');
        
        // Show loading state
        modsElement.innerHTML = '<p>Loading mods...</p>';
        
        fetch('/api/game/mods', {
            headers: {
                'Authorization': `Bearer ${state.token}`
            }
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                // Render mods list
                if (data.mods.length === 0) {
                    modsElement.innerHTML = '<p>No mods installed.</p>';
                } else {
                    let html = '';
                    data.mods.forEach(mod => {
                        const status = mod.enabled ? 
                            '<span class="status online">Enabled</span>' : 
                            '<span class="status offline">Disabled</span>';
                        
                        html += `
                            <div class="list-item">
                                <div>
                                    <strong>${mod.name}</strong>
                                    ${status}
                                </div>
                                <div>
                                    <small>${mod.path}</small>
                                </div>
                            </div>
                        `;
                    });
                    modsElement.innerHTML = html;
                }
            } else {
                modsElement.innerHTML = `<p class="error">${data.error || 'Failed to load mods'}</p>`;
            }
        })
        .catch(error => {
            console.error('Error loading mods data:', error);
            modsElement.innerHTML = `<p class="error">Failed to connect to server</p>`;
        });
    }

    // Load inventory for marketplace listings
    function loadInventoryForListings() {
        // This would be populated with actual inventory data
        const itemIdSelect = document.getElementById('listing-item-id');
        const itemNameInput = document.getElementById('listing-item-name');
        
        if (itemIdSelect && itemNameInput) {
            fetch('/api/player/inventory', {
                headers: {
                    'Authorization': `Bearer ${state.token}`
                }
            })
            .then(response => response.json())
            .then(data => {
                if (data.success) {
                    // Clear existing options
                    itemIdSelect.innerHTML = '';
                    
                    // Add options for each inventory item
                    data.items.forEach(item => {
                        const option = document.createElement('option');
                        option.value = item.itemId;
                        option.textContent = `${item.itemName} x${item.quantity}`;
                        option.dataset.name = item.itemName;
                        option.dataset.condition = item.condition;
                        itemIdSelect.appendChild(option);
                    });
                    
                    // Handle selection change
                    itemIdSelect.addEventListener('change', () => {
                        const selected = itemIdSelect.options[itemIdSelect.selectedIndex];
                        if (selected) {
                            itemNameInput.value = selected.dataset.name || '';
                            
                            // Update condition slider if present
                            const conditionSlider = document.getElementById('listing-condition');
                            const conditionValue = document.getElementById('condition-value');
                            
                            if (conditionSlider && conditionValue && selected.dataset.condition) {
                                const condition = parseFloat(selected.dataset.condition);
                                conditionSlider.value = condition;
                                conditionValue.textContent = `${Math.round(condition * 100)}%`;
                            }
                        }
                    });
                    
                    // Trigger change event for the first item
                    if (itemIdSelect.options.length > 0) {
                        itemIdSelect.selectedIndex = 0;
                        itemIdSelect.dispatchEvent(new Event('change'));
                    }
                }
            })
            .catch(error => {
                console.error('Error loading inventory for listings:', error);
            });
        }
    }

    // Update trade dropdown with friends
    function updateTradeDropdown(friends) {
        const tradeUsernameSelect = document.getElementById('trade-username');
        
        if (tradeUsernameSelect) {
            // Clear existing options
            tradeUsernameSelect.innerHTML = '';
            
            // Add options for each online friend
            const onlineFriends = friends.filter(friend => friend.isOnline);
            
            if (onlineFriends.length === 0) {
                const option = document.createElement('option');
                option.disabled = true;
                option.selected = true;
                option.textContent = 'No online friends';
                tradeUsernameSelect.appendChild(option);
            } else {
                onlineFriends.forEach(friend => {
                    const option = document.createElement('option');
                    option.value = friend.username;
                    option.textContent = friend.username;
                    tradeUsernameSelect.appendChild(option);
                });
            }
        }
    }

    // Add friend functionality
    function addFriend() {
        const username = document.getElementById('friend-username').value;
        
        if (!username) {
            showNotification('Error', 'Username is required', 'error');
            return;
        }
        
        fetch('/api/friends/add', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${state.token}`
            },
            body: JSON.stringify({
                username
            })
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Success', 'Friend request sent!', 'success');
                document.getElementById('friend-username').value = '';
                loadFriendsData();
            } else {
                showNotification('Error', data.error || 'Failed to send friend request', 'error');
            }
        })
        .catch(error => {
            console.error('Error sending friend request:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    }

    // Create marketplace listing
    function createListing() {
        const itemId = document.getElementById('listing-item-id').value;
        const itemName = document.getElementById('listing-item-name').value;
        const quantity = parseInt(document.getElementById('listing-quantity').value);
        const price = parseInt(document.getElementById('listing-price').value);
        const condition = parseFloat(document.getElementById('listing-condition').value);
        
        if (!itemId || !itemName || isNaN(quantity) || isNaN(price)) {
            showNotification('Error', 'All fields are required', 'error');
            return;
        }
        
        if (quantity <= 0) {
            showNotification('Error', 'Quantity must be a positive number', 'error');
            return;
        }
        
        if (price <= 0) {
            showNotification('Error', 'Price must be a positive number', 'error');
            return;
        }
        
        fetch('/api/marketplace/create', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${state.token}`
            },
            body: JSON.stringify({
                itemId,
                itemName,
                quantity,
                price,
                condition
            })
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Success', 'Listing created successfully!', 'success');
                loadMarketplaceData();
            } else {
                showNotification('Error', data.error || 'Failed to create listing', 'error');
            }
        })
        .catch(error => {
            console.error('Error creating listing:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    }

    // Initiate trade
    function initiateTrade(username) {
        let targetUsername = username;
        
        if (!targetUsername) {
            const select = document.getElementById('trade-username');
            if (select) {
                targetUsername = select.value;
            }
        }
        
        if (!targetUsername) {
            showNotification('Error', 'Username is required', 'error');
            return;
        }
        
        fetch('/api/trade/initiate', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${state.token}`
            },
            body: JSON.stringify({
                targetUsername
            })
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Success', 'Trade initiated!', 'success');
                switchTab('trade');
                loadTradeData();
            } else {
                showNotification('Error', data.error || 'Failed to initiate trade', 'error');
            }
        })
        .catch(error => {
            console.error('Error initiating trade:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    }

    // Show notification
    function showNotification(title, message, type = 'info') {
        const container = document.getElementById('notification-container');
        
        // Create notification element
        const notification = document.createElement('div');
        notification.className = `notification ${type}`;
        notification.innerHTML = `
            <h3>${title}</h3>
            <p>${message}</p>
        `;
        
        // Add to container
        container.appendChild(notification);
        
        // Auto-remove after 5 seconds
        setTimeout(() => {
            notification.style.animation = 'fadeOut 0.3s ease forwards';
            setTimeout(() => {
                notification.remove();
            }, 300);
        }, 5000);
    }

    // UI visibility functions
    function showLoginForm() {
        elements.loginForm.style.display = 'block';
    }
    
    function hideLoginForm() {
        elements.loginForm.style.display = 'none';
    }
    
    function showRegisterForm() {
        elements.registerForm.style.display = 'block';
    }
    
    function hideRegisterForm() {
        elements.registerForm.style.display = 'none';
    }
    
    function showDashboard() {
        elements.dashboard.style.display = 'block';
    }
    
    function hideDashboard() {
        elements.dashboard.style.display = 'none';
    }

    // Global functions - these need to be accessible from HTML
    window.acceptFriendRequest = function(username) {
        fetch('/api/friends/accept', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${state.token}`
            },
            body: JSON.stringify({
                username
            })
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Success', 'Friend request accepted!', 'success');
                loadFriendsData();
            } else {
                showNotification('Error', data.error || 'Failed to accept friend request', 'error');
            }
        })
        .catch(error => {
            console.error('Error accepting friend request:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    };
    
    window.declineFriendRequest = function(username) {
        fetch('/api/friends/decline', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${state.token}`
            },
            body: JSON.stringify({
                username
            })
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Success', 'Friend request declined', 'success');
                loadFriendsData();
            } else {
                showNotification('Error', data.error || 'Failed to decline friend request', 'error');
            }
        })
        .catch(error => {
            console.error('Error declining friend request:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    };
    
    window.removeFriend = function(username) {
        fetch('/api/friends/remove', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${state.token}`
            },
            body: JSON.stringify({
                username
            })
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Success', 'Friend removed', 'success');
                loadFriendsData();
            } else {
                showNotification('Error', data.error || 'Failed to remove friend', 'error');
            }
        })
        .catch(error => {
            console.error('Error removing friend:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    };
    
    window.purchaseListing = function(listingId) {
        fetch('/api/marketplace/purchase', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${state.token}`
            },
            body: JSON.stringify({
                listingId
            })
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Success', 'Item purchased successfully!', 'success');
                loadMarketplaceData();
                loadInventoryData();
            } else {
                showNotification('Error', data.error || 'Failed to purchase item', 'error');
            }
        })
        .catch(error => {
            console.error('Error purchasing item:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    };
    
    window.cancelListing = function(listingId) {
        fetch('/api/marketplace/cancel', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${state.token}`
            },
            body: JSON.stringify({
                listingId
            })
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Success', 'Listing cancelled successfully', 'success');
                loadMarketplaceData();
            } else {
                showNotification('Error', data.error || 'Failed to cancel listing', 'error');
            }
        })
        .catch(error => {
            console.error('Error cancelling listing:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    };
    
    window.confirmTradeOffer = function() {
        fetch('/api/trade/confirm', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${state.token}`
            }
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Success', 'Trade offer confirmed', 'success');
                loadTradeData();
            } else {
                showNotification('Error', data.error || 'Failed to confirm trade offer', 'error');
            }
        })
        .catch(error => {
            console.error('Error confirming trade offer:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    };
    
    window.cancelTrade = function() {
        fetch('/api/trade/cancel', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${state.token}`
            }
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Success', 'Trade cancelled', 'success');
                loadTradeData();
            } else {
                showNotification('Error', data.error || 'Failed to cancel trade', 'error');
            }
        })
        .catch(error => {
            console.error('Error cancelling trade:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    };
    
    window.removeTradeItem = function(itemId) {
        fetch('/api/trade/update', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'Authorization': `Bearer ${state.token}`
            },
            body: JSON.stringify({
                action: 'remove',
                itemId
            })
        })
        .then(response => response.json())
        .then(data => {
            if (data.success) {
                showNotification('Success', 'Item removed from trade', 'success');
                loadTradeData();
            } else {
                showNotification('Error', data.error || 'Failed to remove item from trade', 'error');
            }
        })
        .catch(error => {
            console.error('Error removing trade item:', error);
            showNotification('Error', 'Failed to connect to server', 'error');
        });
    };
    
    window.sellItem = function(itemId, itemName) {
        // Open dialog to create listing
        const listingItemIdSelect = document.getElementById('listing-item-id');
        const listingItemNameInput = document.getElementById('listing-item-name');
        
        if (listingItemIdSelect && listingItemNameInput) {
            // Find the option with this item ID
            for (let i = 0; i < listingItemIdSelect.options.length; i++) {
                if (listingItemIdSelect.options[i].value === itemId) {
                    listingItemIdSelect.selectedIndex = i;
                    listingItemIdSelect.dispatchEvent(new Event('change'));
                    break;
                }
            }
            
            // Switch to marketplace tab
            switchTab('marketplace');
            
            // Scroll to create listing section
            document.querySelector('#tab-marketplace .card:nth-child(3)').scrollIntoView({
                behavior: 'smooth'
            });
            
            // Focus on quantity field
            document.getElementById('listing-quantity').focus();
        }
    };
    
    // Try to restore session from localStorage
    const savedToken = localStorage.getItem('kenshiToken');
    if (savedToken) {
        state.token = savedToken;
    }
});