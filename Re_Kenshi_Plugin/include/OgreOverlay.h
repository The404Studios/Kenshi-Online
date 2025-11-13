#pragma once

#include <string>
#include <memory>

// Forward declarations for OGRE types
// We'll include actual OGRE headers in the .cpp file
namespace Ogre {
    class Overlay;
    class OverlayContainer;
    class OverlayManager;
    class RenderWindow;
    class SceneManager;
}

namespace ReKenshi {

/**
 * OGRE Overlay System - hooks into Kenshi's rendering pipeline
 */
class OgreOverlay {
public:
    OgreOverlay();
    ~OgreOverlay();

    // Initialization
    bool Initialize();
    void Shutdown();

    // Rendering
    void Render(float deltaTime);

    // Visibility
    void Show();
    void Hide();
    bool IsVisible() const { return m_visible; }

    // OGRE access
    Ogre::Overlay* GetOverlay() { return m_overlay; }
    Ogre::OverlayContainer* GetRootPanel() { return m_rootPanel; }

private:
    // Hook into Kenshi's OGRE instance
    bool FindOgreInstance();
    bool CreateOverlay();

    // OGRE objects
    Ogre::Overlay* m_overlay;
    Ogre::OverlayContainer* m_rootPanel;
    Ogre::OverlayManager* m_overlayManager;
    Ogre::RenderWindow* m_renderWindow;
    Ogre::SceneManager* m_sceneManager;

    bool m_initialized;
    bool m_visible;
};

} // namespace ReKenshi
