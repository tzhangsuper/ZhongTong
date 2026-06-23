import pygame
import math
import numpy as np
from configparser import ConfigParser

class WheelInput(object):
    def __init__(self):
        # Initialize pygame and joystick
        pygame.init()
        pygame.joystick.init()

        joystick_count = pygame.joystick.get_count()
        if joystick_count > 1:
            raise ValueError("Please Connect Just One Joystick")

        self._joystick = pygame.joystick.Joystick(0)
        self._joystick.init()

        # Load the configuration file for the wheel controls
        self._parser = ConfigParser()
        self._parser.read('wheel_config.ini')
        self._steer_idx = int(self._parser.get('G923 Racing Wheel', 'steering_wheel'))
        self._throttle_idx = int(self._parser.get('G923 Racing Wheel', 'throttle'))
        self._brake_idx = int(self._parser.get('G923 Racing Wheel', 'brake'))

        self._P_idx = int(self._parser.get('G923 Racing Wheel', 'P'))
        self._D_idx = int(self._parser.get('G923 Racing Wheel', 'D'))
        self._N_idx = int(self._parser.get('G923 Racing Wheel', 'N'))
        self._R_idx = int(self._parser.get('G923 Racing Wheel', 'R'))

    def _parse_wheel_input(self):
        # Get axis inputs for steering and throttle
        num_axes = self._joystick.get_numaxes()
        js_inputs = [float(self._joystick.get_axis(i)) for i in range(num_axes)]
        jsButtons = [float(self._joystick.get_button(i)) for i in
                     range(self._joystick.get_numbuttons())]
        # Process steering input
        K1 = 1.0
        steer_cmd = K1 * math.tan(1.1 * js_inputs[self._steer_idx])

        # Process throttle input
        K2 = 1.6
        throttle_cmd = K2 + (2.05 * math.log10(-0.7 * js_inputs[self._throttle_idx] + 1.4) - 1.2) / 0.92
        throttle_cmd = max(0, min(1, throttle_cmd))  # Clamp throttle to [0, 1]

        # Process brake input
        brake_cmd = 1.6 + (2.05 * math.log10(-0.7 * js_inputs[self._brake_idx] + 1.4) - 1.2) / 0.92
        brake_cmd = max(0, min(1, brake_cmd))  # Clamp brake to [0, 1]

        flag = self._handle_automatic_gear(jsButtons)
        data = np.asarray([steer_cmd, throttle_cmd, brake_cmd])
        status = data * flag
        return status[0], status[1], status[2]

    def _handle_automatic_gear(self, js_buttons):
        """Handle automatic transmission based on throttle and button inputs"""
        # Check if we are in 'P', 'N', 'D', or 'R'
        if js_buttons[self._P_idx] == 0 and js_buttons[self._D_idx] == 0 and js_buttons[self._N_idx] == 0 and js_buttons[self._R_idx] == 0:
            status = [1, 0, 1]
        elif js_buttons[self._P_idx] == 1:
            status = [1, 0, 1]
        elif js_buttons[self._D_idx] == 1:
            status = [1, 1, 1]
        elif js_buttons[self._N_idx] == 1:
            status = [1, 0, 1]
        elif js_buttons[self._R_idx] == 1:
            status = [1, -1, 1]
        return np.asarray(status)

    def run(self):
        clock = pygame.time.Clock()
        while True:
            # Limit the loop speed
            clock.tick(60)

            # Parse wheel input and print the values
            steer_cmd, throttle_cmd, brake_cmd = self._parse_wheel_input()
            print(f"Steer: {steer_cmd:.3f}, Throttle: {throttle_cmd:.3f}, Brake: {brake_cmd:.3f}")

            # Handle pygame events
            for event in pygame.event.get():
                if event.type == pygame.QUIT:
                    pygame.quit()
                    return

if __name__ == "__main__":
    wheel_input = WheelInput()
    wheel_input.run()

