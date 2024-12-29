# WiFi Smart Water Pump Controller S1

![Smart Water Pump Controller S1](images/CShow1.gif)

Introduction

The [WiFi Smart Water Pump Controller S1](https://waterlevel.pro/products/WiFi-Smart-Water-Pump-Controller-S1)  is an innovative device 
engineered to provide seamless control of water pumps through WiFi connectivity. 
Designed to integrate effortlessly with the [WiFi Water Level S1](https://github.com/rrguardo/WiFi-Water-Level-S1), this controller 
offers a comprehensive solution for managing water levels and automating pump operations. 
Whether for residential, agricultural, or industrial applications, it enables efficient and remote 
management of water systems.

Please note that this is an incomplete ongoing project. software part are yet uncompleted.

The project is open source and open hardware, licensed under GPLv3. All hardware, software, and
3D printable components were designed from scratch by me, **Raul Rodriguez Guardo**, and are released 
under the GPLv3 license, allowing for free use and modification.

<div align="center">
  <img src="images/gplv3.png" alt="Imagen 2" width="200"/>
  <img src="images/openhardware.jpg" alt="Imagen 1" width="120" style="margin-right: 10px;margin-left: 50px;"/>
</div>

---

## Hardware part at OSHWLab

[WiFi Smart Water Pump Controller S1](https://oshwlab.com/rrguardo83/automatic-water-pump-for-s1)

![PCB](images/C1_PCB.png)

### Where buy the PCB board?
[At OSHWLab project, opening the EasyEDA](https://oshwlab.com/rrguardo83/automatic-water-pump-for-s1) 
editor can in few click buy the entire board with all the PCB components including the components assembly, 
directly from **JLCPCB** and **LCSC** services.

### PCB Component Roles Diagram

![S1 PCB Features](images/C1_PCB_Details.png)

---

## Microcontroller Software

[ArduinoIDE INO File](MicrocontrollerCode.ino)

This devide use ESP32-C3 microcontroller can use Arduino IDE to flash/upload microcontroller code, using **ESP32C3 Dev Module** as board.

Before upload the code, generate a new private and public keys in developer zone at https://waterlevel.pro/settings  (TODO)

---
## FreeCad 3D printable case components

[3D printable case components](3dcase)

For more details about important 3d printing recommendations 
 [read this tips](3dcase/3DPrintReadme.md).


![Smart Water Pump Controller S1](images/2.png)

![Smart Water Pump Controller S1](images/3.png)

![Smart Water Pump Controller S1](images/1.png)

---
# TODO:

* check case when internet is lost and pump on
* automatic device link
* improve the web UI
* minimal opensource server sample
* ...

## Disclaimer

This open-source electronics project is provided **"as is."** No warranties of any kind, 
either express or implied, including but not limited to implied warranties of 
merchantability or fitness for a particular purpose, are made. Use this project at 
your own risk. We are not responsible for any damage, loss, or inconvenience that 
may arise from the use of this project.

---

<div align="center">
  <h5>Designed in the Dominican Republic 2024</h5>
  <img src="images/rd.gif" alt="RD" width="70px" />
</div>

---