import socket
import struct

import numpy as np


thetaToPulse = np.array([209, 209, -209, 209, 209, -209, 209, 209, -209, -209,
                         209, 209, 209, -209, -209, -209, -209, 209, -209, 209,
                         209])


pulseToTheta = 1./thetaToPulse


sensorStruct = struct.Struct('21h21h4h4h6h')
controlStruct = struct.Struct('21h')


hoap_init = np.array([0, 40,  3697,  9537, -5840, -344,  18810, -2000,  0,  8800,
                      0, 40, -3727, -9536,  5809,  425, -18810,  2000,  0, -8800,
                      418])


hoap_l_bound = np.array([-19019, -6479, -14839, -209, -12749, -5225, -19019, -20064, -19019, -209,
                         -6479, -4389, -17138, -27170, -12749, -5225, -31559, -209, -19019, -24035,
                         209])

hoap_l_bound += 209

hoap_u_bound = np.array([6479, 4389, 17138, 27170, 12249, 5225, 31559, 209, 19019, 24035,
                         19019, 6479, 14839, 209, 12749, 5225, 19019, 20064, 19019, 209,
                         18810])

hoap_u_bound -= 209


class FakeHoap3(object):
  def __init__(self, host, port, mb):
    # compute index to make hoap -> rbdyn and rbdyn -> hoap
    self.index = np.array([mb.jointPosInParam(mb.jointIndexById(i)) for i in range(1, 22)])

    self.curQ = hoap_init
    self.curA = np.zeros(21)


  def __enter__(self):
    pass


  def __exit__(self):
    pass


  def open(self):
    pass


  def close(self):
    pass


  def control(self, q, alpha):
    q = np.array(q).reshape((21,))
    qHoap = np.rad2deg(q[self.index])*thetaToPulse
    self.curQ = np.minimum(np.maximum(np.array(qHoap, dtype='short'), hoap_l_bound), hoap_u_bound)

    alpha = np.array(alpha).reshape((21,))
    aHoap = np.rad2deg(alpha[self.index])*thetaToPulse
    self.curA = np.array(aHoap, dtype='short')


  def sensor(self):
    q = np.deg2rad(self.curQ*pulseToTheta)
    qn = q.copy()
    qn[self.index] = q
    qn = np.mat(qn).T

    a = np.deg2rad(self.curA*pulseToTheta)
    an = a.copy()
    an[self.index] = a
    an = np.mat(an).T

    return qn, an



class NetHoap3(object):
  def __init__(self, host, port, mb):
    self.host = (host, port)

    # compute index to make hoap -> rbdyn and rbdyn -> hoap
    self.index = np.array([mb.jointPosInParam(mb.jointIndexById(i)) for i in range(1, 22)])

    self.sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)


  def __enter__(self):
    self.open()


  def __exit__(self):
    self.close()


  def open(self):
    self.sock.connect(self.host)
    print 'Connected'


  def close(self):
    self.sock.close()
    print 'Disconnected'


  def control(self, q, alpha):
    qHoap = np.rad2deg(np.array(q)[self.index].T)*thetaToPulse
    qnHoap = np.array(qHoap, dtype='short').reshape((21,))

    qnHoap = np.minimum(np.maximum(qnHoap, hoap_l_bound), hoap_u_bound)

    self.sock.sendall(controlStruct.pack(*qnHoap))


  def sensor(self):
    data = self.sock.recv(sensorStruct.size)
    if data == None or len(data) != sensorStruct.size:
      raise RuntimeError('Connexion broken')

    pyData = sensorStruct.unpack(data)

    q = np.deg2rad(np.array(pyData[:21])*pulseToTheta)
    qn = q.copy()
    qn[self.index] = q
    qn = np.mat(qn).T

    a = np.deg2rad(np.array(pyData[21:21+21])*pulseToTheta)
    an = a.copy()
    an[self.index] = a
    an = np.mat(an).T

    return qn, an

