import pyautogui
import serial
import argparse
import time
import logging
import os
import subprocess

class MyControllerMap:
    def __init__(self):
        self.button = {'espaco': 'space', 'control':'ctrl','direita':'right','esquerda':'left','shift':'shift','aumenta':'up','diminui': 'down','Shuffle':'s','Refresh':'r'} # Fast forward (10 seg) pro Youtube # Fast forward (10 seg) pro Youtube

class SerialControllerInterface:
    # Protocolo
    # byte 1 -> Botão 1 (estado - Apertado 1 ou não 0)
    # byte 2 -> EOP - End of Packet -> valor reservado 'X'

    def __init__(self, port, baudrate):
        self.ser = serial.Serial('COM5', baudrate=baudrate)
        self.mapping = MyControllerMap()
        self.incoming = '0'
        pyautogui.PAUSE = 0  ## remove delay
    
    def update(self):
        ## Sync protocol
        print("update")   
        while self.incoming != b'X':
            self.incoming = self.ser.read()
            logging.debug("Received INCOMING: {}".format(self.incoming))
            print("lendo")

        data = self.ser.read()
        logging.debug("Received DATA: {}".format(data))

        if data == b'P':
            print("Play")
            pyautogui.keyDown(self.mapping.button['espaco'])
            pyautogui.keyUp(self.mapping.button['espaco'])
        elif data == b'F':
            print("Forward")
            pyautogui.hotkey(self.mapping.button['control'],self.mapping.button['direita'])
        elif data == b'B':
            print("Backward")
            pyautogui.hotkey(self.mapping.button['control'],self.mapping.button['esquerda'])
        elif data == b'S':
            print("Shuffle")
            pyautogui.hotkey(self.mapping.button['control'],self.mapping.button['Shuffle'])
        elif data == b'R':
            print("Refresh")
            pyautogui.hotkey(self.mapping.button['control'],self.mapping.button['Refresh'])
    
        self.incoming = self.ser.read()

    


class DummyControllerInterface:
    def __init__(self):
        self.mapping = MyControllerMap()

    def update(self):
        pyautogui.keyDown(self.mapping.button['A'])
        time.sleep(0.1)
        pyautogui.keyUp(self.mapping.button['A'])
        logging.info("[Dummy] Pressed A button")
        time.sleep(1)


if __name__ == '__main__':
    interfaces = ['dummy', 'serial']
    controller = SerialControllerInterface(port="COM5", baudrate=115200)
    '''n_teve_handshake= True
    while n_teve_handshake:
        n_teve_handshake = controller.handshake()'''
    while True:
        controller.update()
        
    