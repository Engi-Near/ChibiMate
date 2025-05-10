#!/usr/bin/env python3
"""
ChibiMate - Desktop Companion
---------------------------------------
This application displays an animated character in a frameless, transparent window
with furniture interaction capabilities.
"""

import os
import sys
import glob
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

class Furniture:
    """Class to represent a furniture item with its properties"""
    
    def __init__(self, filename, use_point, use_type, x_offset=0, y_offset=0):
        self.filename = filename
        self.use_point = use_point  # (x, y) tuple for character positioning
        self.use_type = use_type    # "sit" or "laying"
        self.x_offset = x_offset    # Offset from default x position
        self.y_offset = y_offset    # Offset from default y position
        self.pixmap = None
        self.position = (0, 0)      # Current position on screen
        self.visible = False
        self.width = 0
        self.height = 0
        self.load_image()
        
        # Create a separate window for the furniture
        self.window = QWidget()
        # Set window flags for furniture - make it stay on top but still below character
        self.window.setWindowFlags(Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint | Qt.Tool)
        self.window.setAttribute(Qt.WA_TranslucentBackground)
        self.window.setStyleSheet("background-color: rgba(0, 0, 0, 0)")
        
        # Create a label to display the furniture image
        self.label = QLabel(self.window)
        if self.pixmap:
            self.label.setPixmap(self.pixmap)
            # Make sure the label is positioned at (0,0) in the window
            self.label.setGeometry(0, 0, self.width, self.height)
            self.window.resize(self.width, self.height)
    
    def load_image(self):
        """Load the furniture image"""
        try:
            if os.path.exists(self.filename):
                self.pixmap = QPixmap(self.filename)
                self.width = self.pixmap.width()
                self.height = self.pixmap.height()
                print(f"Loaded furniture: {self.filename} ({self.width}x{self.height})")
            else:
                print(f"Furniture image not found: {self.filename}")
        except Exception as e:
            print(f"Error loading furniture image: {str(e)}")
    
    def set_position(self, x, y):
        """Set the absolute position of the furniture"""
        self.position = (x, y)
        # Update the window position
        if self.window:
            self.window.move(x, y)
    
    def show(self):
        """Show the furniture window and ensure it's on top"""
        if self.window and self.pixmap:
            # Show the window
            self.window.show()
            # Raise the window to ensure it's on top of other applications
            self.window.raise_()
            self.window.activateWindow()
            self.visible = True
    
    def hide(self):
        """Hide the furniture window"""
        if self.window:
            self.window.hide()
            self.visible = False
    
    def get_use_position(self):
        """Get the absolute position for character use point"""
        return (self.position[0] + self.use_point[0], self.position[1] + self.use_point[1])
    
    def get_center_x(self):
        """Get the x center of the furniture"""
        return self.position[0] + (self.width // 2)

class MenuWindow(QDialog):
    """Menu window with options to control the application"""
    
    def __init__(self, parent=None):
        super(MenuWindow, self).__init__(parent)
        self.parent = parent
        
        self.setWindowTitle("ChibiMate")
        self.setStyleSheet("background-color: rgba(50, 50, 50, 255)")
        self.setFixedSize(300, 250)  # Increased height to accommodate new buttons
        
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
        
        # Furniture label
        furniture_label = QLabel("Add Furniture:")
        furniture_label.setStyleSheet("color: white;")
        layout.addWidget(furniture_label, 2, 0, 1, 2)
        
        # Add couch button
        add_couch_btn = QPushButton("Add Couch")
        add_couch_btn.setStyleSheet("color: white; background-color: #444;")
        add_couch_btn.clicked.connect(lambda: self.add_furniture("couch"))
        layout.addWidget(add_couch_btn, 3, 0)
        
        # Add table button
        add_table_btn = QPushButton("Add Table")
        add_table_btn.setStyleSheet("color: white; background-color: #444;")
        add_table_btn.clicked.connect(lambda: self.add_furniture("table"))
        layout.addWidget(add_table_btn, 3, 1)
        
        # Close menu button
        close_menu_btn = QPushButton("Close Menu")
        close_menu_btn.setStyleSheet("color: white; background-color: #444;")
        close_menu_btn.clicked.connect(self.close)
        layout.addWidget(close_menu_btn, 4, 0)
        
        # Quit application button
        quit_app_btn = QPushButton("Quit")
        quit_app_btn.setStyleSheet("color: white; background-color: #700;")
        quit_app_btn.clicked.connect(self.quit_application)
        layout.addWidget(quit_app_btn, 4, 1)
        
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
    
    def add_furniture(self, furniture_type):
        """Add furniture to the scene using the specified furniture type
        
        Args:
            furniture_type (str): Type of furniture to create (e.g., "couch", "table")
        """
        if self.parent and furniture_type in self.parent.furniture_types:
            # Get default offsets from furniture_types if they exist
            furniture_props = self.parent.furniture_types.get(furniture_type, {})
            x_offset = furniture_props.get("x_offset", 0)
            y_offset = furniture_props.get("y_offset", 0)
            
            # Create the furniture with the default offsets from its configuration
            self.parent.create_furniture(furniture_type, x_offset, y_offset)
            self.parent.start_walk_to_furniture()
    
    def quit_application(self):
        """Quit the application"""
        QApplication.quit()

class TransparentGifViewer(QWidget):
    """A transparent window that displays GIF animations"""
    
    animation_error = pyqtSignal(str)
    
    def __init__(self):
        super(TransparentGifViewer, self).__init__()
        
        # Ensure the character window is always on top
        self.setWindowTitle("ChibiMate")
        self.setWindowFlags(Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint)
        self.setAttribute(Qt.WA_TranslucentBackground)
        self.setStyleSheet("background-color: rgba(30, 30, 30, 30)")
        
        # Main layout
        layout = QVBoxLayout()
        layout.setContentsMargins(0, 0, 0, 0)
        self.setLayout(layout)
        
        # Create layers (from bottom to top: wall, floor, furniture, character)
        self.wall_label = QLabel(self)
        self.floor_label = QLabel(self)
        self.character_label = QLabel(self)
        
        # Set alignment for all layers
        self.wall_label.setAlignment(Qt.AlignBottom)
        self.floor_label.setAlignment(Qt.AlignBottom)
        self.character_label.setAlignment(Qt.AlignCenter)
        
        # Add layers to layout
        layout.addWidget(self.wall_label)
        layout.addWidget(self.floor_label)
        layout.addWidget(self.character_label)
        
        # Set layers to be stacked on top of each other
        self.wall_label.setGeometry(QRect(0, 0, 0, 0))
        self.floor_label.setGeometry(QRect(0, 0, 0, 0))
        self.character_label.setGeometry(QRect(0, 0, 0, 0))
        
        # Animation state variables
        self.current_gif_index = 0
        self.current_frame = 0
        self.frames = []
        self.durations = []
        self.animation_timer = QTimer(self)
        self.animation_timer.timeout.connect(self.next_frame)
        
        # Dragging state
        self.dragging = False
        self.drag_position = None
        self.setMouseTracking(True)
        
        # Variables to store state during dragging
        self.pre_drag_gif_index = 0
        self.pre_drag_auto_mode = False
        self.pre_drag_moving = False
        self.pre_drag_move_direction = "right"
        self.pre_drag_behavior = None
        self.pick_gif_index = -1  # Index of the "pick" GIF
        
        # Furniture variables
        self.current_furniture = None
        self.furniture_target_x = 0
        
        self.animation_error.connect(self.show_error)
        
        self.gif_files = []
        self.load_gif_files()
        
        # Behavior variables
        self.current_behavior = "wait"  # wait, walk, walk_to_furniture, use_furniture
        
        # New variables for automatic mode
        self.auto_mode = False
        self.auto_timer = QTimer(self)
        self.auto_timer.timeout.connect(self.auto_change_state)
        self.move_direction = "right"  # Direction of movement
        self.facing_direction = "right"  # Direction character is facing (may differ from movement)
        self.move_speed = 5
        self.move_timer = QTimer(self)
        self.move_timer.timeout.connect(self.move_window)
        self.moving = False
        self.flipped_frames = []
        
        # Create menu window (but don't show it yet)
        self.menu_window = MenuWindow(self)
        self.menu_visible = False
        
        # Dictionary mapping gif types to their filenames
        self.gif_types = {
            "wait": None,
            "walk": None,
            "sit": None,
            "laying": None,
            "pick": None
        }
        
        # Add furniture types dictionary to store available furniture
        self.furniture_types = {
            "couch": {
                "filename": "couch.png",
                "use_point": (100, 200),
                "use_type": "sit",
                "x_offset": 0,
                "y_offset": -70
            }
        }
        
        # Add furniture state timer for minimum duration
        self.furniture_use_timer = QTimer(self)
        self.furniture_use_timer.setSingleShot(True)
        self.furniture_use_timer.timeout.connect(self.finish_using_furniture)
        
        # Track whether the character just walked to furniture
        self.walked_to_furniture = False
        
        # Load the GIFs and categorize them
        if self.gif_files:
            self.categorize_gifs()
            self.load_current_gif()
            self.show()
        else:
            print("No GIF files found in the embedded resources!")
            
    def load_gif_files(self):
        """Find all GIF and WebP files in the current directory"""
        self.gif_files = glob.glob("*.gif") + glob.glob("*.webp")
        print(f"Found {len(self.gif_files)} animation files: {self.gif_files}")
        
        # Find the "pick" GIF index
        for i, filename in enumerate(self.gif_files):
            if "pick" in filename.lower():
                self.pick_gif_index = i
                print(f"Found 'pick' GIF at index {i}: {filename}")
                break
    
    def categorize_gifs(self):
        """Categorize GIFs by their type based on filename"""
        for i, filename in enumerate(self.gif_files):
            name = filename.lower()
            if "wait" in name:
                self.gif_types["wait"] = i
                print(f"Found wait GIF: {filename}")
            elif "move" in name or "walk" in name:
                self.gif_types["walk"] = i
                print(f"Found walk GIF: {filename}")
            elif "sit" in name:
                self.gif_types["sit"] = i
                print(f"Found sit GIF: {filename}")
            elif "lay" in name:
                self.gif_types["laying"] = i
                print(f"Found laying GIF: {filename}")
            elif "pick" in name:
                self.gif_types["pick"] = i
                print(f"Found pick GIF: {filename}")
    
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
            self.character_label.setPixmap(self.frames[0])
            self.character_label.resize(self.frames[0].size())
            
            self.start_animation()
            
        except Exception as e:
            self.animation_error.emit(f"Error loading GIF: {str(e)}")
            import traceback
            traceback.print_exc()
    
    def create_furniture(self, furniture_type=None, x_offset=None, y_offset=None):
        """Create a furniture item at a position on screen
        
        Args:
            furniture_type (str, optional): Type of furniture to create ("couch" or "table").
                                          If None, a random type is selected.
            x_offset (int, optional): Horizontal offset from default position.
            y_offset (int, optional): Vertical offset from default position.
        """
        # Get screen dimensions
        desktop = QApplication.desktop()
        screen_rect = desktop.availableGeometry(self)
        
        # Choose furniture type if not specified
        if furniture_type is None:
            # For now, default to couch if no selection made
            furniture_type = "couch"
        
        # Ensure the furniture type exists
        if furniture_type not in self.furniture_types:
            print(f"Unknown furniture type: {furniture_type}, defaulting to couch")
            furniture_type = "couch"
            
        # Get furniture properties
        furniture_props = self.furniture_types[furniture_type]
        filename = furniture_props["filename"]
        use_point = furniture_props["use_point"]
        use_type = furniture_props["use_type"]
        
        # Use specified offsets or defaults from furniture_types
        default_x_offset = furniture_props.get("x_offset", 0)
        default_y_offset = furniture_props.get("y_offset", 0)
        
        # If x_offset/y_offset is provided as argument, use it, otherwise use the default from furniture_types
        final_x_offset = x_offset if x_offset is not None else default_x_offset
        final_y_offset = y_offset if y_offset is not None else default_y_offset
        
        # Create the furniture object with the offsets
        self.current_furniture = Furniture(filename, use_point, use_type, final_x_offset, final_y_offset)
        
        # Get furniture dimensions
        furniture_width = self.current_furniture.width
        furniture_height = self.current_furniture.height
        
        # Calculate the available screen space to ensure furniture doesn't clip out
        min_x = screen_rect.left() + 20  # 20px margin from left edge
        max_x = screen_rect.right() - furniture_width - 20  # 20px margin from right edge
        
        # Generate a random position along the x-axis within the valid range
        base_furniture_x = random.randint(min_x, max_x)
        
        # Apply x_offset to the randomized position
        base_furniture_x += final_x_offset
        
        # Calculate the y-position to align bottoms
        char_pos = self.pos()
        char_height = self.height()
        char_bottom = char_pos.y() + char_height
        
        # Align furniture bottom with character bottom, then apply y_offset
        base_furniture_y = char_bottom - furniture_height + final_y_offset
        
        # Ensure the furniture stays within screen bounds after offsets
        final_furniture_x = max(min_x, min(base_furniture_x, max_x))
        final_furniture_y = max(screen_rect.top() + 20, min(base_furniture_y, screen_rect.bottom() - furniture_height - 20))
        
        # Set the furniture position and show it
        self.current_furniture.set_position(final_furniture_x, final_furniture_y)
        self.current_furniture.show()
        
        # Ensure proper window stacking - first bring furniture to front, then character
        QTimer.singleShot(10, lambda: self.current_furniture.window.raise_())
        QTimer.singleShot(20, lambda: self.raise_window_above_furniture())
        
        print(f"Created {furniture_type} at ({final_furniture_x}, {final_furniture_y}) with offsets ({final_x_offset}, {final_y_offset})")
        
        # Set target X for character to move to
        self.furniture_target_x = self.current_furniture.get_center_x()
        
        # Reset walked_to_furniture flag
        self.walked_to_furniture = False
        
        # Start walking to furniture if in auto mode
        if self.auto_mode:
            self.start_walk_to_furniture()
    
    def render_furniture(self):
        """Check if furniture should be visible and update its state"""
        if not self.current_furniture:
            return
            
        desktop = QApplication.desktop()
        screen_rect = desktop.availableGeometry(self)
        
        # Check if furniture position is in view
        furniture_left = self.current_furniture.position[0]
        furniture_right = furniture_left + self.current_furniture.width
        
        if furniture_left < screen_rect.right() and furniture_right > screen_rect.left():
            # Furniture is in view
            if not self.current_furniture.visible:
                self.current_furniture.show()
        else:
            # Furniture is out of view
            if self.current_furniture.visible:
                self.current_furniture.hide()
    
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
        
        # If not moving, update the frame based on the facing direction
        if not self.moving:
            if self.facing_direction == "right":
                self.character_label.setPixmap(self.frames[self.current_frame])
            else:  # facing left
                self.character_label.setPixmap(self.flipped_frames[self.current_frame])
        
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
        
        # Explicitly ensure movement is stopped
        self.moving = False
        
        # Reset behavior to wait
        self.current_behavior = "wait"
        
        # Keep the current facing direction (don't reset it)
        
        # Set to wait animation
        if self.gif_types["wait"] is not None:
            self.current_gif_index = self.gif_types["wait"]
            self.load_current_gif()
    
    def auto_change_state(self):
        """Randomly choose next behavior in auto mode"""
        # Stop current movement if any
        self.move_timer.stop()
        self.moving = False
        
        # Choose a random duration between 10-20 seconds
        duration = random.randint(10000, 20000)
        self.auto_timer.start(duration)
        
        # Choose between available behaviors
        behaviors = ["wait", "walk"]
        
        # Add walk_to_furniture option if no furniture is present
        if not self.current_furniture:
            behaviors.append("walk_to_furniture")
        # Add use_furniture option only if furniture is visible AND we just walked to it
        elif self.current_furniture and self.current_furniture.visible and self.walked_to_furniture:
            behaviors.append("use_furniture")
        
        # Choose a random behavior
        new_behavior = random.choice(behaviors)
        self.current_behavior = new_behavior
        
        print(f"Auto mode: Switching to behavior: {new_behavior}")
        
        if new_behavior == "wait":
            if self.gif_types["wait"] is not None:
                self.current_gif_index = self.gif_types["wait"]
                self.load_current_gif()
        elif new_behavior == "walk":
            self.start_moving()
        elif new_behavior == "walk_to_furniture":
            # Randomly select furniture type for auto mode
            furniture_types = list(self.furniture_types.keys())
            selected_type = random.choice(furniture_types)
            
            # Get default offsets from furniture configuration
            furniture_props = self.furniture_types.get(selected_type, {})
            x_offset = furniture_props.get("x_offset", 0)
            y_offset = furniture_props.get("y_offset", 0)
            
            self.create_furniture(selected_type, x_offset, y_offset)
            self.start_walk_to_furniture()
        elif new_behavior == "use_furniture":
            self.use_furniture()
        
        # In auto mode, when the state changes away from using furniture, clean up
        self.furniture_use_timer.timeout.disconnect()
        self.furniture_use_timer.timeout.connect(self.finish_using_furniture)
        self.furniture_use_timer.start(duration)  # Use the same duration as the auto timer
    
    def start_moving(self):
        """Start moving the window"""
        self.moving = True
        self.current_behavior = "walk"
        
        # Use the walking animation
        if self.gif_types["walk"] is not None:
            self.current_gif_index = self.gif_types["walk"]
            self.load_current_gif()
        
        # Randomly choose direction and update facing direction to match
        self.move_direction = random.choice(["left", "right"])
        self.facing_direction = self.move_direction
        
        # Start the movement timer
        self.move_timer.start(50)  # Update position every 50ms
    
    def start_walk_to_furniture(self):
        """Start walking towards furniture"""
        if not self.current_furniture:
            return
            
        self.moving = True
        self.current_behavior = "walk_to_furniture"
        
        # Use the walking animation
        if self.gif_types["walk"] is not None:
            self.current_gif_index = self.gif_types["walk"]
            self.load_current_gif()
        
        # Determine initial direction based on furniture position
        character_center_x = self.x() + self.width() // 2
        furniture_center_x = self.current_furniture.get_center_x()
        
        # Set the initial direction and lock it for the duration of the walk
        # Also update the facing direction to match
        if furniture_center_x > character_center_x:
            self.move_direction = "right"
        else:
            self.move_direction = "left"
            
        self.facing_direction = self.move_direction
        
        print(f"Starting walk to furniture in {self.move_direction} direction")
        
        # Start the movement timer
        self.move_timer.start(50)  # Update position every 50ms
    
    def use_furniture(self):
        """Use the current furniture"""
        if not self.current_furniture:
            return
            
        self.current_behavior = "use_furniture"
        self.moving = False
        self.move_timer.stop()
        
        # Get the use point of the furniture
        use_x, use_y = self.current_furniture.get_use_position()
        
        # Position the character at the use point
        # Character should be centered horizontally on the use point 
        # and positioned so its bottom aligns with the bottom of the furniture
        self.move(use_x - self.width() // 2, use_y - self.height())
        
        # Ensure character window is above furniture
        self.raise_window_above_furniture()
        
        # Use the appropriate animation based on furniture type
        use_type = self.current_furniture.use_type
        if use_type == "sit" and self.gif_types["sit"] is not None:
            self.current_gif_index = self.gif_types["sit"]
            self.load_current_gif()
        elif use_type == "laying" and self.gif_types["laying"] is not None:
            self.current_gif_index = self.gif_types["laying"]
            self.load_current_gif()
            
        # Different behavior based on auto mode vs manual mode
        if self.auto_mode:
            # In auto mode, use furniture for the random duration determined in auto_change_state
            # This is already set by auto_change_state, so we don't need to start another timer
            print("Using furniture in auto mode - will end with next state change")
        else:
            # In manual mode, don't set a timer - the user will need to change state manually
            # Stop the furniture use timer if it's running
            self.furniture_use_timer.stop()
            print("Using furniture in manual mode - press spacebar to change")
    
    def finish_using_furniture(self):
        """Called when the furniture use timer expires"""
        print("Finished using furniture, removing it")
        
        # Remove the furniture
        if self.current_furniture:
            self.current_furniture.hide()
            self.current_furniture = None
            
        # Reset the walked_to_furniture flag
        self.walked_to_furniture = False
        
        # Explicitly stop all movement
        self.moving = False
        self.move_timer.stop()
        
        # Keep the current facing direction (don't reset it)
        
        # Go back to wait state
        self.current_behavior = "wait"
        if self.gif_types["wait"] is not None:
            self.current_gif_index = self.gif_types["wait"]
            self.load_current_gif()
        
        # Note: Auto mode transition is handled by auto_change_state now
    
    def move_window(self):
        """Move the window in the current direction"""
        if not self.moving:
            return
        
        pos = self.pos()
        desktop = QApplication.desktop()
        screen_rect = desktop.availableGeometry(self)
        window_rect = self.frameGeometry()
        
        # Check if window is about to leave the screen and reverse direction if needed
        if self.current_behavior == "walk":
            if self.move_direction == "right" and window_rect.right() + self.move_speed > screen_rect.right():
                # About to hit right edge, change direction
                self.move_direction = "left"
                self.facing_direction = "left"  # Update facing direction too
                print("Reached right screen edge, reversing direction")
            elif self.move_direction == "left" and window_rect.left() - self.move_speed < screen_rect.left():
                # About to hit left edge, change direction
                self.move_direction = "right"
                self.facing_direction = "right"  # Update facing direction too
                print("Reached left screen edge, reversing direction")
        
        # Render furniture if exists
        if self.current_furniture:
            self.render_furniture()
        
        # Handle different movement behaviors
        if self.current_behavior == "walk_to_furniture" and self.current_furniture:
            # Calculate center point of character
            character_center_x = pos.x() + self.width() // 2
            
            # Check if we've reached the target (furniture center)
            if abs(character_center_x - self.furniture_target_x) < self.move_speed * 2:
                # Reached furniture, start using it
                print("Reached furniture, switching to use_furniture")
                # Set the flag indicating we just walked to furniture
                self.walked_to_furniture = True
                self.use_furniture()
                return
            
            # No direction changes in walk_to_furniture mode - keep the initial direction
            # This prevents the back-and-forth flipping issue
        
        # Move in the current direction and update the character frame
        if self.move_direction == "right":
            # Move right and show normal frames
            self.character_label.setPixmap(self.frames[self.current_frame])
            self.move(pos.x() + self.move_speed, pos.y())
            self.facing_direction = "right"  # Ensure facing direction is updated
        else:
            # Move left and show flipped frames
            self.character_label.setPixmap(self.flipped_frames[self.current_frame])
            self.move(pos.x() - self.move_speed, pos.y())
            self.facing_direction = "left"  # Ensure facing direction is updated
            
        # Ensure character stays above furniture during movement
        if self.current_furniture and self.current_furniture.visible:
            self.raise_window_above_furniture()
    
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
                # In manual mode, if currently using furniture, pressing space should end it
                if self.current_behavior == "use_furniture" and self.current_furniture:
                    self.finish_using_furniture()
                else:
                    self.next_gif()
        elif event.key() == Qt.Key_A:
            self.toggle_auto_mode()
        elif event.key() == Qt.Key_M:
            self.toggle_menu()
        elif event.key() == Qt.Key_F and not self.auto_mode:
            # Manual furniture creation for testing - create a couch using default offsets
            furniture_type = "couch"
            furniture_props = self.furniture_types.get(furniture_type, {})
            x_offset = furniture_props.get("x_offset", 0)
            y_offset = furniture_props.get("y_offset", 0)
            self.create_furniture(furniture_type, x_offset, y_offset)
            self.start_walk_to_furniture()  # Start walking to the furniture
        elif event.key() == Qt.Key_T and not self.auto_mode:
            # Manual furniture creation for testing - create a table using default offsets
            furniture_type = "table"
            if furniture_type in self.furniture_types:
                furniture_props = self.furniture_types.get(furniture_type, {})
                x_offset = furniture_props.get("x_offset", 0)
                y_offset = furniture_props.get("y_offset", 0)
                self.create_furniture(furniture_type, x_offset, y_offset)
                self.start_walk_to_furniture()  # Start walking to the furniture
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
            self.pre_drag_behavior = self.current_behavior
            
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
            
            # Delete furniture if it exists
            if self.current_furniture:
                print("Removing furniture during pick")
                self.current_furniture.hide()
                self.current_furniture = None
            
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
            
            # Default to wait state after pick
            if self.gif_types["wait"] is not None:
                self.current_gif_index = self.gif_types["wait"]
                self.current_behavior = "wait"
                # Keep current facing direction
                self.load_current_gif()
                print("Defaulting to wait state after pick")
            
            # Restore auto mode if it was active
            if self.pre_drag_auto_mode:
                self.auto_mode = True
                self.auto_change_state()
                print("Auto mode restored after dragging")

    def closeEvent(self, event):
        """Handle window close event"""
        # Make sure to clean up furniture window when main window closes
        if self.current_furniture:
            self.current_furniture.hide()
            self.current_furniture = None
        event.accept()

    def raise_window_above_furniture(self):
        """Ensure character window is always above furniture window
        
        This method explicitly raises the character window above all other windows,
        including the furniture window, to maintain proper visual layering.
        """
        # First, lower the window to reset z-order, then raise it to ensure it's at the top
        self.lower()
        self.raise_()
        
        # Explicitly activate window to bring it to the front
        self.activateWindow()
        
        # For extra assurance, if furniture exists, raise character window after a slight delay
        if self.current_furniture and self.current_furniture.visible:
            # Use a single-shot timer to ensure character window gets raised after furniture
            QTimer.singleShot(10, lambda: self.activateWindow())
            QTimer.singleShot(10, lambda: self.raise_())


if __name__ == "__main__":
    app = QApplication(sys.argv)
    
    print(f"Python version: {sys.version}")
    print(f"PyQt5 is being used for the UI")
    print(f"Using Qt platform plugin: {app.platformName()}")
    
    viewer = TransparentGifViewer()
    
    sys.exit(app.exec_()) 