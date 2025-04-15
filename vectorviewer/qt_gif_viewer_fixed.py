#!/usr/bin/env python3
"""
Qt GIF Viewer with Advanced Transparency
---------------------------------------
This application displays GIF animations in a frameless, transparent window.
It uses PyQt5 specifically for better compatibility across environments.
"""

import os
import sys
import random
import pkg_resources

if 'QT_QPA_PLATFORM_PLUGIN_PATH' in os.environ:
    del os.environ['QT_QPA_PLATFORM_PLUGIN_PATH']

try:
    from PyQt5.QtWidgets import (QApplication, QWidget, QLabel, QVBoxLayout, 
                                QPushButton, QMainWindow, QDialog, QGridLayout, 
                                QDesktopWidget)
    from PyQt5.QtCore import Qt, QTimer, QSize, QPoint, pyqtSignal, QRect
    from PyQt5.QtGui import QPixmap, QImage, QKeyEvent, QMouseEvent, QTransform, QMovie
except ImportError:
    print("PyQt5 is required but not installed.")
    print("Install it with: pip install PyQt5")
    sys.exit(1)

# Define the GIF files that will be embedded
EMBEDDED_GIFS = [
    'vectorlying.webp',
    'vectorsit.webp',
    'vectormove.webp',
    'vectorpick.webp',
    'vectorwait.webp'
]

class MenuWindow(QDialog):
    """Menu window with options to control the application"""
    
    def __init__(self, parent=None):
        super(MenuWindow, self).__init__(parent)
        self.parent = parent
        
        self.setWindowTitle("ChibiMate")
        self.setStyleSheet("background-color: rgba(50, 50, 50, 255)")
        self.setFixedSize(300, 200)
        
        # Center relative to the parent window
        if parent:
            self.move(parent.x() + parent.width() // 2 - self.width() // 2,
                     parent.y() + parent.height() // 2 - self.height() // 2)
        
        # Layout
        layout = QGridLayout()
        self.setLayout(layout)
        
        # Menu title
        title = QLabel("ChibiMate")
        title.setStyleSheet("color: white; font-size: 18px; font-weight: bold;")
        title.setAlignment(Qt.AlignCenter)
        layout.addWidget(title, 0, 0, 1, 2)
        
        # Toggle auto mode button
        self.auto_mode_btn = QPushButton("Toggle Auto Mode")
        self.auto_mode_btn.setStyleSheet("color: white; background-color: #444;")
        self.auto_mode_btn.clicked.connect(self.toggle_auto_mode)
        layout.addWidget(self.auto_mode_btn, 1, 0, 1, 2)
        
        # Next GIF button
        next_gif_btn = QPushButton("Next GIF")
        next_gif_btn.setStyleSheet("color: white; background-color: #444;")
        next_gif_btn.clicked.connect(self.next_gif)
        layout.addWidget(next_gif_btn, 2, 0, 1, 2)
        
        # Close menu button
        close_menu_btn = QPushButton("Close Menu")
        close_menu_btn.setStyleSheet("color: white; background-color: #444;")
        close_menu_btn.clicked.connect(self.close)
        layout.addWidget(close_menu_btn, 3, 0)
        
        # Quit application button
        quit_app_btn = QPushButton("Quit")
        quit_app_btn.setStyleSheet("color: white; background-color: #700;")
        quit_app_btn.clicked.connect(self.quit_application)
        layout.addWidget(quit_app_btn, 3, 1)
        
        self.update_button_text()
        
    def update_button_text(self):
        """Update button text based on current state"""
        if self.parent and hasattr(self.parent, "auto_mode"):
            status = "ON" if self.parent.auto_mode else "OFF"
            self.auto_mode_btn.setText(f"Auto Mode: {status}")
    
    def toggle_auto_mode(self):
        """Toggle auto mode from the menu"""
        if self.parent:
            self.parent.toggle_auto_mode()
            self.update_button_text()
    
    def next_gif(self):
        """Show next gif from the menu"""
        if self.parent:
            self.parent.next_gif()
    
    def quit_application(self):
        """Quit the application"""
        QApplication.quit()

class TransparentGifViewer(QWidget):
    """A transparent window that displays GIF animations"""
    
    animation_error = pyqtSignal(str)
    
    def __init__(self):
        super(TransparentGifViewer, self).__init__()
        
        self.setWindowTitle("GIF Viewer")
        self.setWindowFlags(Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint)
        self.setAttribute(Qt.WA_TranslucentBackground)
        self.setStyleSheet("background-color: rgba(30, 30, 30, 30)")
        
        layout = QVBoxLayout()
        layout.setContentsMargins(0, 0, 0, 0)
        self.setLayout(layout)
        
        self.image_label = QLabel(self)
        self.image_label.setAlignment(Qt.AlignCenter)
        layout.addWidget(self.image_label)
        
        self.current_gif_index = 0
        self.current_frame = 0
        self.frames = []
        self.durations = []
        self.animation_timer = QTimer(self)
        self.animation_timer.timeout.connect(self.next_frame)
        
        self.dragging = False
        self.drag_position = None
        self.setMouseTracking(True)
        
        # Variables to store state during dragging
        self.pre_drag_gif_index = 0
        self.pre_drag_auto_mode = False
        self.pre_drag_moving = False
        self.pre_drag_move_direction = "right"
        self.pre_drag_auto_timer_active = False
        self.pre_drag_auto_timer_remaining = 0
        self.pre_drag_move_timer_active = False
        self.pick_gif_index = -1  # Index of the "pick" GIF
        
        self.animation_error.connect(self.show_error)
        
        self.gif_files = EMBEDDED_GIFS
        
        # New variables for automatic mode
        self.auto_mode = False
        self.auto_timer = QTimer(self)
        self.auto_timer.timeout.connect(self.auto_change_state)
        self.move_direction = "right"
        self.move_speed = 5
        self.move_timer = QTimer(self)
        self.move_timer.timeout.connect(self.move_window)
        self.moving = False
        self.flipped_frames = []
        
        # Create menu window (but don't show it yet)
        self.menu_window = MenuWindow(self)
        self.menu_visible = False
        
        if self.gif_files:
            self.load_current_gif()
            self.show()
        else:
            print("No GIF files found in the embedded resources!")
            
    def crop_image(self, img):
        """Crop the top 30% of the image and shift up"""
        width, height = img.size().width(), img.size().height()
        crop_top = int(height * 0.3)
        return img.copy(QRect(0, crop_top, width, height - crop_top))
    
    def load_current_gif(self):
        """Load and display the current GIF"""
        if not self.gif_files:
            return
            
        self.animation_timer.stop()
        
        current_file = self.gif_files[self.current_gif_index]
        print(f"Loading: {current_file}")
        
        try:
            # Load the GIF from embedded resources
            if getattr(sys, 'frozen', False):
                # Running in PyInstaller bundle
                base_path = sys._MEIPASS
            else:
                # Running in normal Python environment
                base_path = os.path.dirname(os.path.abspath(__file__))
                
            gif_path = os.path.join(base_path, current_file)
            img = QMovie(gif_path)
            
            self.frames = []
            self.flipped_frames = []
            self.durations = []
            
            frame_count = img.frameCount()
            
            img.jumpToFrame(0)
            
            for i in range(frame_count):
                try:
                    current = img.currentImage()
                    
                    current = self.crop_image(current)
                    
                    pixmap = QPixmap.fromImage(current)
                    
                    # Also create flipped version for left movement
                    flipped_pixmap = pixmap.transformed(QTransform().scale(-1, 1))
                    
                    self.frames.append(pixmap)
                    self.flipped_frames.append(flipped_pixmap)
                    self.durations.append(img.nextFrameDelay())
                    
                    if i < frame_count - 1:
                        img.jumpToFrame(i + 1)
                except EOFError:
                    break
            
            if not self.frames:
                self.animation_error.emit(f"No frames found in {current_file}")
                return
                
            self.resize(self.frames[0].width(), self.frames[0].height())
            
            self.current_frame = 0
            self.image_label.setPixmap(self.frames[0])
            self.image_label.resize(self.frames[0].size())
            
            self.start_animation()
            
        except Exception as e:
            self.animation_error.emit(f"Error loading GIF: {str(e)}")
            import traceback
            traceback.print_exc()
    
    def show_error(self, message):
        """Display error message"""
        print(f"ERROR: {message}")
    
    def start_animation(self):
        """Start the animation timer"""
        if self.frames and self.durations:
            self.animation_timer.start(self.durations[0])
            
    def next_frame(self):
        """Display the next frame in the animation"""
        if not self.frames:
            return
            
        self.current_frame = (self.current_frame + 1) % len(self.frames)
        
        # If in auto mode and moving, the move_window method handles displaying the correct frame
        if not (self.auto_mode and self.moving):
            self.image_label.setPixmap(self.frames[self.current_frame])
        
        duration = self.durations[self.current_frame] if self.current_frame < len(self.durations) else 100
        self.animation_timer.setInterval(duration)
    
    def next_gif(self):
        """Load the next GIF in the list"""
        if self.gif_files:
            self.current_gif_index = (self.current_gif_index + 1) % len(self.gif_files)
            self.load_current_gif()
    
    def toggle_auto_mode(self):
        """Toggle between automatic and manual mode"""
        self.auto_mode = not self.auto_mode
        print(f"Auto mode: {'ON' if self.auto_mode else 'OFF'}")
        
        if self.auto_mode:
            self.start_auto_mode()
        else:
            self.stop_auto_mode()
    
    def start_auto_mode(self):
        """Start automatic mode behavior"""
        self.auto_change_state()
        
    def stop_auto_mode(self):
        """Stop automatic mode behavior"""
        self.auto_timer.stop()
        self.move_timer.stop()
        self.moving = False
    
    def auto_change_state(self):
        """Randomly choose between moving and showing a random gif"""
        # Stop current movement if any
        self.move_timer.stop()
        self.moving = False
        
        # Choose a random duration between 10-20 seconds
        duration = random.randint(10000, 20000)
        self.auto_timer.start(duration)
        
        # 50% chance to move, 50% chance to show random gif
        if random.choice([True, False]):
            self.start_moving()
        else:
            # Show random gif (not the current one)
            if len(self.gif_files) > 1:
                new_index = self.current_gif_index
                while new_index == self.current_gif_index:
                    new_index = random.randint(0, len(self.gif_files) - 1)
                self.current_gif_index = new_index
                self.load_current_gif()
    
    def start_moving(self):
        """Start moving the window"""
        self.moving = True
        
        # Find and load the walking animation if available
        move_gif_index = None
        for i, filename in enumerate(self.gif_files):
            if "move" in filename.lower() or "walk" in filename.lower():
                move_gif_index = i
                break
        
        if move_gif_index is not None:
            self.current_gif_index = move_gif_index
            self.load_current_gif()
        
        # Randomly choose direction
        self.move_direction = random.choice(["left", "right"])
        
        # Start the movement timer
        self.move_timer.start(50)  # Update position every 50ms
    
    def move_window(self):
        """Move the window in the current direction"""
        if not self.moving:
            return
        
        pos = self.pos()
        desktop = QApplication.desktop()
        screen_rect = desktop.availableGeometry(self)
        window_rect = self.frameGeometry()
        
        # Check if window is about to leave the screen and reverse direction if needed
        if self.move_direction == "right" and window_rect.right() + self.move_speed > screen_rect.right():
            # About to hit right edge, change direction
            self.move_direction = "left"
            print("Reached right screen edge, reversing direction")
        elif self.move_direction == "left" and window_rect.left() - self.move_speed < screen_rect.left():
            # About to hit left edge, change direction
            self.move_direction = "right" 
            print("Reached left screen edge, reversing direction")
        
        # Move in the current direction
        if self.move_direction == "right":
            # Show normal frames and move right
            self.image_label.setPixmap(self.frames[self.current_frame])
            self.move(pos.x() + self.move_speed, pos.y())
        else:
            # Show flipped frames and move left
            self.image_label.setPixmap(self.flipped_frames[self.current_frame])
            self.move(pos.x() - self.move_speed, pos.y())
    
    def toggle_menu(self):
        """Toggle the menu window visibility"""
        if self.menu_visible:
            self.menu_window.hide()
            self.menu_visible = False
        else:
            # Update button text before showing
            self.menu_window.update_button_text()
            # Center relative to the main window
            self.menu_window.move(self.x() + self.width() // 2 - self.menu_window.width() // 2,
                                 self.y() + self.height() // 2 - self.menu_window.height() // 2)
            self.menu_window.show()
            self.menu_visible = True
    
    def keyPressEvent(self, event):
        """Handle key press events"""
        if event.key() == Qt.Key_Space:
            if not self.auto_mode:
                self.next_gif()
        elif event.key() == Qt.Key_A:
            self.toggle_auto_mode()
        elif event.key() == Qt.Key_M:
            self.toggle_menu()
        elif event.key() == Qt.Key_Escape:
            self.close()
        else:
            super().keyPressEvent(event)
    
    def mousePressEvent(self, event):
        """Handle mouse press events for dragging"""
        if event.button() == Qt.LeftButton:
            # Save complete current state
            self.pre_drag_gif_index = self.current_gif_index
            self.pre_drag_auto_mode = self.auto_mode
            self.pre_drag_moving = self.moving
            self.pre_drag_move_direction = self.move_direction
            
            # Save timer states
            self.pre_drag_auto_timer_active = self.auto_timer.isActive()
            if self.pre_drag_auto_timer_active:
                self.pre_drag_auto_timer_remaining = self.auto_timer.remainingTime()
                
            self.pre_drag_move_timer_active = self.move_timer.isActive()
            
            # Start dragging
            self.dragging = True
            self.drag_position = event.globalPos() - self.frameGeometry().topLeft()
            
            # Pause all current activity
            self.auto_timer.stop()
            self.move_timer.stop()
            self.moving = False
            
            if self.auto_mode:
                print(f"Auto mode paused for dragging (moving: {self.pre_drag_moving}, direction: {self.pre_drag_move_direction})")
            
            # Switch to the "pick" GIF if available
            if self.pick_gif_index != -1:
                self.current_gif_index = self.pick_gif_index
                self.load_current_gif()
                print("Switched to 'pick' GIF for dragging")
            
            event.accept()
    
    def mouseMoveEvent(self, event):
        """Handle mouse move events for dragging"""
        if event.buttons() & Qt.LeftButton and self.dragging:
            self.move(event.globalPos() - self.drag_position)
            event.accept()
    
    def mouseReleaseEvent(self, event):
        """Handle mouse release events"""
        if self.dragging:
            # End dragging state
            self.dragging = False
            
            # Restore previous GIF if we switched to pick GIF
            if self.current_gif_index == self.pick_gif_index:
                self.current_gif_index = self.pre_drag_gif_index
                self.load_current_gif()
                print(f"Restored to previous GIF index {self.pre_drag_gif_index}")
            
            # Restore previous auto mode if it was active
            if self.pre_drag_auto_mode:
                self.auto_mode = True
                
                # Restore movement state if it was moving
                if self.pre_drag_moving:
                    self.moving = True
                    self.move_direction = self.pre_drag_move_direction
                    if self.pre_drag_move_timer_active:
                        self.move_timer.start(50)  # Restart movement timer
                        print(f"Restored movement in direction: {self.move_direction}")
                
                # Restore auto timer with remaining time if it was active
                if self.pre_drag_auto_timer_active:
                    # Use either remaining time or a default if time already passed
                    remaining_time = max(self.pre_drag_auto_timer_remaining, 1000)
                    self.auto_timer.start(remaining_time)
                    print(f"Restored auto timer with {remaining_time}ms remaining")
                else:
                    # If timer wasn't active, restart auto behavior
                    self.start_auto_mode()
                
                print("Auto mode state fully restored after dragging")
            

if __name__ == "__main__":
    app = QApplication(sys.argv)
    
    print(f"Python version: {sys.version}")
    print(f"PyQt5 is being used for the UI")
    print(f"Using Qt platform plugin: {app.platformName()}")
    
    viewer = TransparentGifViewer()
    
    sys.exit(app.exec_()) 