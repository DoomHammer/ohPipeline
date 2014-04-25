#!/usr/bin/env python
"""TestAirplayFunctions - test Net Aux source functionality.
 
Parameters:
    arg#1 - DUT ['local' for internal SoftPlayer]
    arg#2 - iTunes server (PC name)
    
NOTES
    - allow delay after setting DACP speaker (or iTunes (ver >= 11) may hang)
            
This verifies output by the DUT using Net Aux decode is correct, and Net Aux 
source handling operates correctly.
""" 

# Differences from DS test:
#    - removed all testing that measured output on scope
#    - removed auto-select mode checking (mode N/A)

import _FunctionalTest
import BaseTest                       as BASE
import Upnp.ControlPoints.Volkano     as Volkano
import Instruments.Network.DacpClient as DacpClient
import _SoftPlayer                    as SoftPlayer
import LogThread
import math
import sys
import threading
import time

kItunesCtr = 'Music'
kVolTrack  = 'Stereo1kHz-441-16-0dB-72m'


class TestAirplayFunctions( BASE.BaseTest ):
    """Class to verify Net Aux source (Airplay input) functionality"""
    
    def __init__( self ):
        """Constructor for Net Aux test"""
        BASE.BaseTest.__init__( self )
        self.dacp       = None
        self.dut        = None
        self.dutName    = None
        self.dutDev     = None
        self.soft       = None
        self.tracks     = None
        self.timedOut   = False
        self.timeEvent  = threading.Event()
        self.srcChanged = threading.Event()

    def Test( self, args ):
        """Net Aux test"""
        itunesServer = None

        # parse command line arguments
        try:
            self.dutName = args[1]
            itunesServer = args[2]
        except:
            print '\n', __doc__, '\n'
            self.log.Abort( '', 'Invalid arguments %s' % (str( args )) )

        if self.dutName.lower() == 'local':
            self.soft = SoftPlayer.SoftPlayer( aRoom='TestDev' )
            self.dutName = 'TestDev:SoftPlayer'
            time.sleep( 5 )     # allow time for iTunes to discover device

        # setup self.dacp (iTunes) control
        self.dacp = DacpClient.DacpClient( itunesServer )
        if self.dutName not in self.dacp.speakers:
            self.log.Abort( self.dacp.dev, '%s not available as iTunes speaker' % self.dutName )
        self.dacp.speaker = 'My Computer'
        time.sleep( 5 )        
        self.dacp.Pause()
                    
        # setup DUT
        self.dutDev = self.dutName.split( ':' )[0]
        self.dut = Volkano.VolkanoDevice( self.dutName, aIsDut=False )
        self.dut.product.AddSubscriber( self.__ProductEvtCb )
                        
        # get list of available tracks from iTunes
        if self.dacp.library in self.dacp.databases:
            self.dacp.database = self.dacp.library 
            if kItunesCtr in self.dacp.containers:
                self.dacp.container = kItunesCtr
                self.tracks = self.dacp.items
            else:
                self.log.Abort( self.dacp.dev, '%s container not found in iTunes' % kItunesCtr )
        else: 
            self.log.Abort( self.dacp.dev, '%s database not found in iTunes' % self.dacp.library )

        # run the tests
        self._CheckSelect()
        self._CheckAudioFreq()
        self._CheckVolume()

    def Cleanup( self ):
        """Perform cleanup on test exit"""
        if self.dacp:
            try:
                self.dacp.Pause()  
                self.dacp.speaker = 'My Computer'
            except:
                pass
        if self.dut: self.dut.Shutdown()
        if self.soft: self.soft.Shutdown()
        BASE.BaseTest.Cleanup( self )

    def _CheckSelect( self ):
        """Check auto-selection of Net Aux source"""
        self.dacp.speaker = 'My Computer'
        self.dacp.PlayTrack( kVolTrack )
        time.sleep( 5 )
         
        # Airplay connect/disconnect 
        # DS on Playlist -> Set as Airplay speaker -> Playlist reselected -> Airplay disconnected 
        self.srcChanged.clear()
        self.dut.product.sourceIndexByName = 'Playlist'
        self.srcChanged.wait( 5 )
        name = self.dut.product.SourceSystemName( self.dut.product.sourceIndex )
        self.log.FailUnless( self.dutDev, name=='Playlist',
            '%s/Playlist - actual/expected source before Airplay started' % name )
         
        self.srcChanged.clear()
        self.dacp.speaker = self.dutName
        self.srcChanged.wait( 5 )
        name = self.dut.product.SourceSystemName( self.dut.product.sourceIndex )
        self.log.FailUnless( self.dutDev, name=='Net Aux',
            '%s/Net Aux - actual/expected source on Airplay start' % name )
         
        self.srcChanged.clear()
        self.dut.product.sourceIndexByName = 'Playlist'
        self.srcChanged.wait( 5 )
        name = self.dut.product.SourceSystemName( self.dut.product.sourceIndex )
        self.log.FailUnless( self.dutDev, name=='Playlist',
            '%s/Playlist - actual/expected source after Playlist re-selected' % name )
         
        self.srcChanged.clear()
        self.dacp.speaker = 'My Computer'
        self.srcChanged.wait( 5 )
        name = self.dut.product.SourceSystemName( self.dut.product.sourceIndex )
        self.log.FailUnless( self.dutDev, name=='Playlist',
            '%s/Playlist - actual/expected source after Airplay disconnected' % name )
         
        # Airplay to DS -> Pause Airplay -> DS to playlist -> start Airplay playback
        # -> reselect Airplay on DS         
         
        self.srcChanged.clear()
        self.dacp.speaker = self.dutName
        self.srcChanged.wait( 5 )
        name = self.dut.product.SourceSystemName( self.dut.product.sourceIndex )
        self.log.FailUnless( self.dutDev, name=='Net Aux',
            '%s/Net Aux - actual/expected source on Airplay re-start (1)' % name )

        self.srcChanged.clear()
        self.dacp.Pause()
        self.srcChanged.wait( 5 )
        name = self.dut.product.SourceSystemName( self.dut.product.sourceIndex )
        self.log.FailUnless( self.dutDev, name=='Net Aux',
            '%s/Net Aux - actual/expected source after Airplay stopped' % name )

        self.srcChanged.clear()
        self.dut.product.sourceIndexByName = 'Playlist'
        self.srcChanged.wait( 5 )
        self.srcChanged.clear()
        self.dacp.PlayTrack( kVolTrack )
        self.srcChanged.wait( 5 )
        name = self.dut.product.SourceSystemName( self.dut.product.sourceIndex )
        self.log.FailUnless( self.dutDev, name=='Playlist',
            '%s/Playlist - actual/expected source after Airplay re-start (2)' % name )
        
        self.srcChanged.clear()
        self.dacp.speaker = 'My Computer'
        time.sleep( 5 ) 
        self.dacp.speaker = self.dutName 
        time.sleep( 5 )
        self.srcChanged.wait( 5 )
        name = self.dut.product.SourceSystemName( self.dut.product.sourceIndex )
        self.log.FailUnless( self.dutDev, name=='Net Aux',
            '%s/Net Aux - actual/expected source after AirPlay speakers re-selected' % name )
        
    def _CheckAudioFreq( self ):
        """Check audio frequency of Net Aux tracks"""
        if self.dut.volume is not None:
            self.dut.volume.volume = 80
        self.srcChanged.clear()
        self.dacp.speaker = self.dutName
        self.srcChanged.wait( 5 )
        self.dacp.volume = 100
        time.sleep( 5 )

        titles = []
        for track in self.tracks:
            if track['album'] == 'Test Tones':
                if '30s' in track['title']:
                    titles.append( track['title'] )
                    
        if len( titles ) < 14:
            self.log.Fail( self.dacp.dev, '%d required test tones not available on %s' % (len( titles ), self.dacp.server) )
                
        for title in titles:
            self.log.Info( '' )
            self.log.Info( 'AirPlay playback with %s' % title )
            self.log.Info( '' )
            self.dacp.PlayTrack( title )
            self._MonitorPlayback( 10 )
            
    def _CheckVolume( self ):
        """Check volume control on server is reflected in DS output"""
        if self.dut.volume is not None:
            self.dut.volume.volume = 80
        self.srcChanged.clear()
        self.dacp.speaker = 'My Computer'
        time.sleep( 5 )
        self.dacp.speaker = self.dutName
        time.sleep( 5 )
        self.dacp.PlayTrack( kVolTrack )
        self.srcChanged.wait( 5 )
        self.dacp.volume = 100
        time.sleep( 5 )
        
        for (setVol, expVol) in [(10,-42), (20,-28), (30,-21), (40,-16), (50,-12),
                                 (60,-9),  (70,-6),  (80,-4),  (90,-2),  (100,0)]:
            self.log.Info ( '' )
            self.log.Info( 'AirPlay volume set to %d' % setVol )
            self.log.Info ( '' )
            self.dacp.volume = setVol
            self._MonitorPlayback( 10 )
            
    def _MonitorPlayback( self, aSecs ):
        """Monitor AirPlay playback"""
        self.dut.time.AddSubscriber( self.__TimeEvtCb )
        self.timedOut = False
        t = LogThread.Timer( aSecs, self.__TimerCb )
        t.start()
        while not self.timedOut:
            self.timeEvent.clear()
            self.timeEvent.wait( 2 )
            if not self.timeEvent.is_set():
                self.log.Fail( self.dutDev, 'No time update for 2s during Airplay playback' )
            else:
                info = self.dacp.nowPlaying
                self.log.Info( self.dacp.dev, info )        
        self.dut.time.RemoveSubscriber( self.__TimeEvtCb )

    @staticmethod
    def _ToDb( aVmeas, aVzero ):
        """Convert amplitude from Vrms to dB"""
        try:
            db = round( 20 * math.log10( aVmeas/aVzero ), 3 )
        except:
            db = -999
        return db

    # noinspection PyUnusedLocal
    def __ProductEvtCb( self, service, svName, svVal, svSeq ):
        """Callback on events from product service"""
        if svName == 'SourceIndex':
            self.srcChanged.set()
            
    # noinspection PyUnusedLocal
    def __TimeEvtCb( self, service, svName, svVal, svSeq ):
        """Callback on events from Time service"""
        self.timeEvent.set()

    def __TimerCb( self ):
        """Timer CB - set flag on expiry"""
        self.timedOut = True
        
if __name__ == '__main__':
    
    BASE.Run( sys.argv )
        