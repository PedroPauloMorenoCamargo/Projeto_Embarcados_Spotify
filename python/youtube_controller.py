import pyautogui
import serial
import argparse
import time
import logging

class MyControllerMap:
    def __init__(self):
        self.button = {'espaco': 'space', 'control':'ctrl','direita':'right','esquerda':'left','shift':'shift','aumenta':'up','diminui': 'down'} # Fast forward (10 seg) pro Youtube # Fast forward (10 seg) pro Youtube

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

        if data == b'1':
            print("datab1 ")
            pyautogui.keyDown(self.mapping.button['espaco'])
        elif data == b'0':
            print("datab0") 
            logging.info("KEYUP ESPACO")
            pyautogui.keyUp(self.mapping.button['espaco'])  

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

    while True:
        controller.update()
