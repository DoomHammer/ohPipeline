#!/usr/bin/env python
"""TestPlaylistPlayback - test Playlist service state/transition basic operation

Parameters:
    arg#1 - DUT ['local' for internal SoftPlayer]
    arg#2 - test mode 
              - all             for all configs sequentially
              - <num>           for config number <num> 
              - [min:max]       for range of (numbered) configs
              - [<n1>,<n2>...]  list of configs
              - <stim>          for stimulus (Play, Pause, Stop,
                                    SeekSecondsAbsolute, SeekSecondsRelative,
                                    SeekIndex, Next, Previous)
              - <pre-state>     for precon state (Stopped, Playing, Paused)
    arg#3 - random number generator seed (0 for 'random')
            by passing this, test scenarious containing random data can be
            replicated - the seed used is logged at the start of execution
            
Tests state-transition operation of DS service. Each transition is tested as a
standalone operation - no aggregation of actions due to async operation tested.
""" 
import _FunctionalTest
import BaseTest                   as BASE
import Upnp.ControlPoints.Volkano as Volkano
import Utils.Network.HttpServer   as HttpServer
import Utils.Common               as Common
import _SoftPlayer                as SoftPlayer
import Path
import os
import random
import sys
import time
import threading

kAudioRoot = os.path.join( Path.AudioDir(), 'MusicTracks/' )
kTrackList = os.path.join( kAudioRoot, 'TrackList.xml' )


class Config:
    "Test configuration for Media service testing"
    
    class Precon:
        "Configuration subclass for precondition info and setup"
        
        def __init__( self, aState, aPlLen, aTrack, aSecs, aRepeat, aTracks, aLog, aDev ):
            "Initialise class data"
            self.state         = aState
            self.plLen         = None
            self.plLen         = self._SubstMacros( aPlLen )
            self.track         = self._SubstMacros( aTrack )
            self.secs          = aSecs
            self.repeat        = aRepeat
            self.duration      = None
            self.durationEvent = threading.Event()
            self.idArrayEvent  = threading.Event()
            self.stoppedEvent  = threading.Event()
            self.pausedEvent   = threading.Event()
            self.playingEvent  = threading.Event()
            self.timeEvent     = threading.Event()
            self.log           = aLog
            self.dev           = aDev
            
            # create random playlist
            self.playlist = []
            for i in range( self.plLen ):
                self.playlist.append( aTracks[random.randint( 0, len( aTracks )-1)])
        
        def Setup( self, aDut ):
            "Setup preconditions on the renderer" 
            aDut.playlist.DeleteAllTracks()
            self.log.Info( self.dev, 'Adding playlist of %d tracks' % self.plLen )
            aDut.playlist.AddSubscriber( self._PlaylistEventCb )
            aDut.playlist.AddPlaylist( self.playlist )
            self.idArrayEvent.wait( 5 );
            self.log.Info( self.dev, 'Added playlist of %d tracks' % self.plLen )
            aDut.playlist.repeat = self.repeat
            aDut.info.AddSubscriber( self._InfoEventCb )
            
            if self.track != -1:
                self.durationEvent.clear()
                self.playingEvent.clear()
                aDut.playlist.SeekIndex( self.track )
                if self.state == 'Stopped':
                    self.stoppedEvent.clear()
                    aDut.playlist.Stop()
                    self.stoppedEvent.wait( 5 )
                    self.duration = None
                elif self.state in ['Playing', 'Paused']:
                    self.playingEvent.wait( 5 )                    
                    if self.secs == '@T':
                        self.durationEvent.wait( 5 )
                        self.duration = aDut.info.duration
                        if not self.durationEvent.isSet():
                            self.log.Info( self.dev, 
                                'Duration event timed-out, assume unchanged (%d)'
                                % self.duration )
                        if self.duration > 10:
                            self.secs = random.randint( 5, self.duration-5 )
                        else:
                            self.log.Warn( self.dev, 'Duration value not updated' )  
                            self.secs = random.randint( 5, 95 )
                        self.playingEvent.clear()
                        aDut.playlist.SeekSecondAbsolute( self.secs )
                        self.playingEvent.wait( 5 ) # wait to avoid race conds on seekSecsRel
                    if self.state == 'Paused':
                        self.pausedEvent.clear()
                        aDut.playlist.Pause()
                        self.pausedEvent.wait( 5 )
            aDut.info.RemoveSubscriber( self._InfoEventCb )
            aDut.playlist.RemoveSubscriber( self._PlaylistEventCb )
            self.log.Info( self.dev, 'Preconditions setup' ) 
            
        def _SubstMacros( self, aArg ):
            """Substitute parameter macros with actual values.
               Valid macros are:
                @m - random track within playlist
                @N - playlist length -> random value between 8 and 30
            """
            try:
                if not self.plLen:
                    aArg = aArg.replace( '@N', 'random.randint( 8, 30 )' )
                else:
                    aArg = aArg.replace( '@N', 'self.plLen' )
                aArg = aArg.replace( '@m', 'random.randint( 2, self.plLen-3 )' )
                subst = eval( aArg )
            except:
                subst = aArg
            return subst
        
        def _InfoEventCb( self, aService, aSvName, aSvVal, aSvSeq ):
            "Callback from Info service events"
            if aSvName == 'Duration' and aSvVal not in [0,'0']:
                self.durationEvent.set()
                
        def _PlaylistEventCb( self, aService, aSvName, aSvVal, aSvSeq ):
            "Callback from Playlist service events"
            if aSvName == 'TransportState':
                if aSvVal == 'Stopped':
                    self.stoppedEvent.set()        
                elif aSvVal == 'Playing':
                    self.playingEvent.set()
                elif aSvVal == 'Paused':
                    self.pausedEvent.set()        
            elif aSvName == 'IdArray' and aSvVal not in [0,'0']:
                self.idArrayEvent.set()        
                
                
    class Stimulus:
        "Configuration subclass for stimulus info and invokation"
        
        def __init__( self, aAction, aMinVal, aMaxVal, aPrecon ):
            "Initialise class data"
            self.action = aAction
            self.precon = aPrecon
            self.minVal = aMinVal
            self.maxVal = aMaxVal
            self.val    = 0
            self.timeEvent= threading.Event()
        
        def Invoke( self, aDut ):
            "Invoke stimulus on specified renderer"
            aDut.time.AddSubscriber( self._timeEventCb )
            if self.action in ['Play','Pause','Stop','Next','Previous']:
                getattr( aDut.playlist, self.action )()
            else:
                min = self._SubstMacros( self.minVal, aDut )
                max = self._SubstMacros( self.maxVal, aDut )
                self.precon.log.Info( self.precon.dev, 'MIN %s  MAX %s' % (str(min), str(max)))

                if min == '':
                    self.val = random.randint( max-100, max )
                elif max == '':
                    self.val = random.randint( min, min+100 )
                elif min == max:
                    self.val = min
                else:
                    self.val = random.randint( min, max )
                getattr( aDut.playlist, self.action )( self.val )
            # either DS will start playback in which case need to wait for event
            # before starting outcome timers, or will not be playing in which
            # case wait will timeout without any affect on the result (#2527)
            time.sleep( 0.5 )
            self.timeEvent.clear()
            self.timeEvent.wait( 5 )            
            aDut.time.RemoveSubscriber( self._timeEventCb )
        
        def _SubstMacros( self, aArg, aDut ):
            """Substitute parameter macros with actual values. Some of these are
               NOT KNOWN at __init__ time, so call the substs at Invoke() time
               Valid macros are:
                @D - current track duration
                @m - current track
                @N - playlist length
                @T - current track secs
            """
            try:
                aArg = aArg.replace( '@D', 'aDut.info.duration' )
                aArg = aArg.replace( '@m', 'self.precon.track' )
                aArg = aArg.replace( '@N', 'self.precon.plLen' )
                aArg = aArg.replace( '@T', 'self.precon.secs' )
                subst = eval( aArg )
            except:
                subst = aArg
            return subst
        
        def _timeEventCb( self, aService, aSvName, aSvVal, aSvSeq ):
            "Callback from Time service events"
            self.timeEvent.set()        

        
    class Outcome:
        "Configuration subclass for outcome checking"
        
        def __init__( self, aId, aState, aTrack, aSecs, aPrecon, aStim ):
            "Initialise class data"
            self.id     = aId
            self.state  = aState
            self.track  = aTrack
            self.secs   = aSecs
            self.precon = aPrecon
            self.stim   = aStim

        def Check( self, aLog, aDut, aDev ):
            "Check outcome on specified renderer"
            delay1   = 3
            delay2   = 5
            expState = self.state
            expTrack = int( self._SubstMacros( self.track ))
            expSecs  = int( self._SubstMacros( self.secs, aDut ))

            # wait a few secs (from time event/timeout) and check values
            time.sleep( delay1 )
            if expState == 'Playing' and aDut.playlist.transportState == 'Playing':            
                expSecs += delay1
                (expState, expTrack, expSecs) = \
                    self._RecalcExpected( aDut, expState, expTrack, expSecs )
            self._CheckValues( aLog, aDut, aDev, expState, expTrack, expSecs, delay1 )

            # wait a few more secs and check that values are changing correctly
            # (if Playing) or not changing as expected (if Paused, Stopped)
            time.sleep( delay2 )
            if expState == 'Playing' and aDut.playlist.transportState == 'Playing':
                expSecs += delay2
                (expState, expTrack, expSecs) = \
                    self._RecalcExpected( aDut, expState, expTrack, expSecs )
            self._CheckValues( 
                aLog, aDut, aDev, expState, expTrack, expSecs, delay1+delay2 )

        def _CheckValues( self, aLog, aDut, aDev, aState, aTrack, aSecs, aAfter ):
            "Check values from DS are as expected"
            
            aLog.Info( '' )
            evtTrack = aDut.playlist.PlaylistIndex( aDut.playlist.id )     
            aLog.FailUnless( aDev, evtTrack==aTrack,
                '[%d] (%d/%d) Actual/Expected EVENTED track index %ds after invoke' %
                (self.id, evtTrack, aTrack, aAfter) )
            
            evtState = aDut.playlist.transportState
            aLog.FailUnless( aDev, evtState==aState,
                '[%d] (%s/%s) Actual/Expected EVENTED transport state %ds after invoke' % 
                (self.id, evtState, aState, aAfter) )
            
            pollState = aDut.playlist.polledTransportState
            aLog.FailUnless( aDev, pollState==aState,
                '[%d] (%s/%s) Actual/Expected POLLED transport state %ds after invoke' % 
                (self.id, pollState, aState, aAfter) )
            
            evtSecs = aDut.time.seconds
            aLog.CheckLimits( aDev, 'GELE', evtSecs, aSecs-2, aSecs+1,
                '[%d] Expected EVENTED track seconds %ds after invoke' % (self.id, aAfter) )
            
            pollSecs = aDut.time.polledSeconds
            aLog.CheckLimits( aDev, 'GELE', pollSecs, aSecs-2, aSecs+1,
                '[%d] Expected POLLED track seconds %ds after invoke' % (self.id, aAfter) )
            
            pollBitDepth = aDut.info.polledBitDepth
            evtBitDepth = aDut.info.bitDepth
            aLog.FailUnless( aDev, pollBitDepth==evtBitDepth,
                '[%d] (%s/%s) Polled/Evented bit depth %ds after invoke' % 
                (self.id, pollBitDepth, evtBitDepth, aAfter) )
            
            pollBitRate = aDut.info.polledBitRate
            evtBitRate = aDut.info.bitRate
            aLog.FailUnless( aDev, pollBitRate==evtBitRate,
                '[%d] (%s/%s) Polled/Evented bit rate %ds after invoke' % 
                (self.id, pollBitRate, evtBitRate, aAfter) )
            
            pollCodecName = aDut.info.polledCodecName
            evtCodecName = aDut.info.codecName
            aLog.FailUnless( aDev, pollCodecName==evtCodecName,
                '[%d] (%s/%s) Polled/Evented codec name %ds after invoke' % 
                (self.id, pollCodecName, evtCodecName, aAfter) )
            
            pollDetailsCount = aDut.info.polledDetailsCount
            evtDetailsCount = aDut.info.detailsCount
            aLog.FailUnless( aDev, pollDetailsCount==evtDetailsCount,
                '[%d] (%s/%s) Polled/Evented details count %ds after invoke' % 
                (self.id, pollDetailsCount, evtDetailsCount, aAfter) )
            
            pollDuration = aDut.info.polledDuration
            evtDuration = aDut.info.duration
            aLog.FailUnless( aDev, pollDuration==evtDuration,
                '[%d] (%s/%s) Polled/Evented duration %ds after invoke' % 
                (self.id, pollDuration, evtDuration, aAfter) )
            
            pollLossless = aDut.info.polledLossless
            evtLossless = aDut.info.lossless
            aLog.FailUnless( aDev, pollLossless==evtLossless,
                '[%d] (%s/%s) Polled/Evented lossless %ds after invoke' % 
                (self.id, pollLossless, evtLossless, aAfter) )
            
            pollMetadata = aDut.info.polledMetadata
            evtMetadata = aDut.info.metadata
            aLog.FailUnless( aDev, pollMetadata==evtMetadata,
                '[%d] (%s/%s) Polled/Evented metadata %ds after invoke' % 
                (self.id, pollMetadata, evtMetadata, aAfter) )
            
            pollMetatext = aDut.info.polledMetatext
            evtMetatext = aDut.info.metatext
            aLog.FailUnless( aDev, pollMetatext==evtMetatext,
                '[%d] (%s/%s) Polled/Evented metatext %ds after invoke' % 
                (self.id, pollMetatext, evtMetatext, aAfter) )
            
            pollMetatextCount = aDut.info.polledMetatextCount
            evtMetatextCount = aDut.info.metatextCount
            aLog.FailUnless( aDev, pollMetatextCount==evtMetatextCount,
                '[%d] (%s/%s) Polled/Evented metatext count %ds after invoke' % 
                (self.id, pollMetatextCount, evtMetatextCount, aAfter) )
            
            pollSampleRate = aDut.info.polledSampleRate
            evtSampleRate = aDut.info.sampleRate
            aLog.FailUnless( aDev, pollSampleRate==evtSampleRate,
                '[%d] (%s/%s) Polled/Evented sample rate %ds after invoke' % 
                (self.id, pollSampleRate, evtSampleRate, aAfter) )
            
            pollTrackCount = aDut.info.polledTrackCount
            evtTrackCount = aDut.info.trackCount
            aLog.FailUnless( aDev, pollTrackCount==evtTrackCount,
                '[%d] (%s/%s) Polled/Evented track count %ds after invoke' % 
                (self.id, pollTrackCount, evtTrackCount, aAfter) )
            
            pollUri = aDut.info.polledUri
            evtUri = aDut.info.uri
            aLog.FailUnless( aDev, pollUri==evtUri,
                '[%d] (%s/%s) Polled/Evented URI %ds after invoke' % 
                (self.id, pollUri, evtUri, aAfter) )
            aLog.Info( '' )     
            
        def _RecalcExpected( self, aDut, aState, aTrack, aSecs ):
            "Recalculate expected values after playback over time"
            expState = aState
            expTrack = aTrack
            expSecs  = aSecs
            if self.precon.duration:
                if expSecs > self.precon.duration:
                    expSecs = expSecs - self.precon.duration
                    expTrack += 1
                    if expTrack >= self.precon.plLen:
                        expTrack = 0
                        if self.precon.repeat == 'off':
                            expState = 'Stopped'
            return (expState, expTrack, expSecs)
        
        def _SubstMacros( self, aArg, aDut=None ):
            """Substitute parameter macros with actual values. Some of these are
               NOT KNOWN at __init__ time, so call the substs at Check() time
               Valid macros are:
                @D - current track duration
                @m - precon track
                @N - playlist length
                @T - precon track secs
                @x - current track
                @y - current track secs
            """
            try:
                aArg = aArg.replace( '@D', 'aDut.info.duration' )
                aArg = aArg.replace( '@N', 'self.precon.plLen' )
                aArg = aArg.replace( '@m', 'self.precon.track' )
                aArg = aArg.replace( '@T', 'self.precon.secs' )
                aArg = aArg.replace( '@x', 'self.stim.val' )
                aArg = aArg.replace( '@y', 'self.stim.val' )
                subst = eval( aArg )
            except:
                subst = aArg
            return subst
                        

    def __init__( self, aLog, aDev, aDut, aConf, aTracks ):
        "Initialise class (and sub-class) data"
        self.log  = aLog
        self.id   = aConf[0]
        self.dut  = aDut
        self.dev  = aDev
        self.pre  = self.Precon( aConf[1], aConf[2], aConf[3],
                                 aConf[4], aConf[5], aTracks, self.log, aDev )
        self.stim = self.Stimulus( aConf[6], aConf[7], aConf[8], self.pre )
        self.out  = self.Outcome( self.id, aConf[9], aConf[10], aConf[11],
                                  self.pre, self.stim )
        
    def Setup( self ):
        self.pre.Setup( self.dut )
        
    def InvokeStimulus( self ):
        self.stim.Invoke( self.dut )
        
    def CheckOutcome( self ):
        self.out.Check( self.log, self.dut, self.dev )


class TestPlaylistPlayback( BASE.BaseTest ):
    "Test playback control operation (using UPnP is control method)"

    def __init__( self ):
        "Constructor - initalise base class"
        BASE.BaseTest.__init__( self )
        self.dut    = None
        self.server = None
        self.soft   = None
                
    def Test( self, aArgs ):
        "DS Service state/transition test"
        # parse command line arguments
        try:
            dutName    = aArgs[1]
            mode       = aArgs[2]
            seed       = int( aArgs[3] )
        except:
            print '\n', __doc__, '\n'
            self.log.Abort( '', 'Invalid arguments %s' % (str( aArgs )) )
                            
        # seed the random number generator
        if not seed:
            seed = int( time.time() ) % 1000000
        self.log.Info( '', 'Seeding random number generator with %d' % (seed) )
        random.seed( seed )
                
        # create renderer CP and subscribe for DS events
        if dutName.lower() == 'local':
            self.soft = SoftPlayer.SoftPlayer( aRoom='TestDev' )
            dutName = 'TestDev:SoftPlayer'
        self.dutDev = dutName.split( ':' )[0]
        self.dut = Volkano.VolkanoDevice( dutName, aIsDut=True )
        self.dut.playlist.shuffle = False
        
        # start audio 
        self.server = HttpServer.HttpServer( kAudioRoot )
        self.server.Start()        
        
        # create test confgurations as specified by mode
        testConfigs = self._GetConfigs( mode )
        
        # test the configurations
        numConfig  = 0
        numConfigs = len( testConfigs )
        for config in testConfigs:
            numConfig += 1
            self.log.Info( '' )
            self.log.Info( '', 'Testing config ID# %d (%d of %d)' % \
                           (config.id, numConfig, numConfigs) )
            self.log.Info( '' )
            config.Setup()
            config.InvokeStimulus()
            config.CheckOutcome()
            
    def Cleanup( self ):
        "Perform post-test cleanup" 
        if self.dut:
            self.dut.playlist.Stop() 
            self.dut.Shutdown()
        if self.server:
            self.server.Shutdown()        
        if self.soft:
            self.soft.Shutdown()
        BASE.BaseTest.Cleanup( self )               

    def _GetConfigs( self, aMode ):
        "Create and return list of test configurations (as filtered by aMode)"       
        tracks  = Common.GetTracks( kTrackList, self.server )
        configs = []
        for entry in configTable:
            selected = False
            if aMode in ('all', 'ALL', 'All'):
                selected = True
                
            elif aMode in ('Play', 'Pause', 'Stop', 'SeekSecondsAbsolute',
                'SeekSecondsRelative', 'SeekIndex', 'Next', 'Previous' ):
                if entry[6] == aMode:
                    selected = True
                    
            elif aMode in ('Stopped', 'Playing', 'Paused'):
                if entry[1] == aMode:
                    selected = True
                    
            elif aMode[0] == '[':
                vals = aMode[1:].split( ':' )
                if len( vals ) == 2:
                    # range of numbered configs in format [nn:mm]
                    try:
                        min = int( vals[0] )
                        max = int( vals[1].strip( ']' ))
                        if min <= entry[0] and max >= entry[0]:
                            selected = True
                    except:
                        pass
                else:
                    vals = aMode[1:-1].split( ',' )
                    if str( entry[0] ) in vals:
                        selected = True
            else:
                try:
                    # individual numbered config
                    if entry[0] == int( aMode ):
                        selected = True
                except:
                    pass

            if selected:
                configs.append( Config( self.log, self.dutDev, self.dut, entry, tracks ))
            
        return configs


configTable = \
    [
    #   | Preconditions                       | Stimulus                                  | Outcome
    # Id| State     PlLen  Track   Secs   Rpt | Action                MinVal     MaxVal   | State      Track         Secs
    (200, 'Stopped', '@N',    '0',  '0', 'off', 'Play'              ,        '',        '', 'Playing',          '0',     '0'),
    (201, 'Stopped', '@N',   '@m',  '0', 'off', 'Play'              ,        '',        '', 'Playing',         '@m',     '0'),
    (202, 'Stopped', '@N', '@N-1',  '0', 'off', 'Play'              ,        '',        '', 'Playing',       '@N-1',     '0'),

    (210, 'Stopped', '@N',    '0',  '0', 'off', 'Pause'             ,        '',        '', 'Stopped',          '0',     '0'),
    (211, 'Stopped', '@N',   '@m',  '0', 'off', 'Pause'             ,        '',        '', 'Stopped',         '@m',     '0'),
    (212, 'Stopped', '@N', '@N-1',  '0', 'off', 'Pause'             ,        '',        '', 'Stopped',       '@N-1',     '0'),
    
    (220, 'Stopped', '@N',    '0',  '0', 'off', 'Stop'              ,        '',        '', 'Stopped',          '0',     '0'),
    (221, 'Stopped', '@N',   '@m',  '0', 'off', 'Stop'              ,        '',        '', 'Stopped',         '@m',     '0'),
    (222, 'Stopped', '@N', '@N-1',  '0', 'off', 'Stop'              ,        '',        '', 'Stopped',       '@N-1',     '0'),

    (230, 'Stopped', '@N',    '0',  '0', 'off', 'SeekSecondAbsolute',       '0',       '0', 'Playing',          '0',     '0'),
    (231, 'Stopped', '@N',    '0',  '0', 'off', 'SeekSecondAbsolute',       '1',        '', 'Playing',          '0',    '@y'),
    (232, 'Stopped', '@N',   '@m',  '0', 'off', 'SeekSecondAbsolute',       '0',       '0', 'Playing',         '@m',     '0'),
    (233, 'Stopped', '@N',   '@m',  '0', 'off', 'SeekSecondAbsolute',       '1',        '', 'Playing',         '@m',    '@y'),
    (234, 'Stopped', '@N', '@N-1',  '0', 'off', 'SeekSecondAbsolute',       '0',       '0', 'Playing',       '@N-1',     '0'),
    (235, 'Stopped', '@N', '@N-1',  '0', 'off', 'SeekSecondAbsolute',       '1',        '', 'Playing',       '@N-1',    '@y'),

    (250, 'Stopped', '@N',    '0',  '0', 'off', 'SeekSecondRelative',       '0',       '0', 'Playing',          '0',     '0'),
    (251, 'Stopped', '@N',    '0',  '0', 'off', 'SeekSecondRelative',       '1',        '', 'Playing',          '0',    '@y'),
    (252, 'Stopped', '@N',    '0',  '0', 'off', 'SeekSecondRelative',        '',      '-1', 'Playing',          '0',     '0'),
    (253, 'Stopped', '@N',   '@m',  '0', 'off', 'SeekSecondRelative',       '0',       '0', 'Playing',         '@m',     '0'),
    (254, 'Stopped', '@N',   '@m',  '0', 'off', 'SeekSecondRelative',       '1',        '', 'Playing',         '@m',    '@y'),
    (255, 'Stopped', '@N',   '@m',  '0', 'off', 'SeekSecondRelative',        '',      '-1', 'Playing',         '@m',     '0'),
    (256, 'Stopped', '@N', '@N-1',  '0', 'off', 'SeekSecondRelative',       '0',       '0', 'Playing',       '@N-1',     '0'),
    (257, 'Stopped', '@N', '@N-1',  '0', 'off', 'SeekSecondRelative',       '1',        '', 'Playing',       '@N-1',    '@y'),
    (258, 'Stopped', '@N', '@N-1',  '0', 'off', 'SeekSecondRelative',        '',      '-1', 'Playing',       '@N-1',     '0'),

    (280, 'Stopped', '@N',    '0',  '0', 'off', 'SeekIndex'         ,       '0',       '0', 'Playing',          '0',     '0'),
    (281, 'Stopped', '@N',    '0',  '0', 'off', 'SeekIndex'         ,       '1',    '@N-2', 'Playing',         '@x',     '0'),
    (282, 'Stopped', '@N',    '0',  '0', 'off', 'SeekIndex'         ,    '@N-1',    '@N-1', 'Playing',       '@N-1',     '0'),
    (283, 'Stopped', '@N',    '0',  '0', 'off', 'SeekIndex'         ,      '@N',        '', 'Stopped',          '0',     '0'),
    (284, 'Stopped', '@N',   '@m',  '0', 'off', 'SeekIndex'         ,       '0',       '0', 'Playing',          '0',     '0'),
    (285, 'Stopped', '@N',   '@m',  '0', 'off', 'SeekIndex'         ,       '1',    '@N-2', 'Playing',         '@x',     '0'),
    (286, 'Stopped', '@N',   '@m',  '0', 'off', 'SeekIndex'         ,    '@N-1',    '@N-1', 'Playing',       '@N-1',     '0'),
    (287, 'Stopped', '@N',   '@m',  '0', 'off', 'SeekIndex'         ,      '@N',        '', 'Stopped',          '0',     '0'),
    (288, 'Stopped', '@N', '@N-1',  '0', 'off', 'SeekIndex'         ,       '0',       '0', 'Playing',          '0',     '0'),
    (289, 'Stopped', '@N', '@N-1',  '0', 'off', 'SeekIndex'         ,       '1',    '@N-2', 'Playing',         '@x',     '0'),
    (290, 'Stopped', '@N', '@N-1',  '0', 'off', 'SeekIndex'         ,    '@N-1',    '@N-1', 'Playing',       '@N-1',     '0'),
    (291, 'Stopped', '@N', '@N-1',  '0', 'off', 'SeekIndex'         ,      '@N',        '', 'Stopped',          '0',     '0'),

    (300, 'Stopped', '@N',    '0',  '0',  'on', 'SeekIndex'         ,       '0',       '0', 'Playing',          '0',     '0'),
    (301, 'Stopped', '@N',    '0',  '0',  'on', 'SeekIndex'         ,       '1',    '@N-2', 'Playing',         '@x',     '0'),
    (302, 'Stopped', '@N',    '0',  '0',  'on', 'SeekIndex'         ,    '@N-1',    '@N-1', 'Playing',       '@N-1',     '0'),
    (303, 'Stopped', '@N',    '0',  '0',  'on', 'SeekIndex'         ,      '@N',        '', 'Stopped',          '0',     '0'),
    (304, 'Stopped', '@N',   '@m',  '0',  'on', 'SeekIndex'         ,       '0',       '0', 'Playing',          '0',     '0'),
    (305, 'Stopped', '@N',   '@m',  '0',  'on', 'SeekIndex'         ,       '1',    '@N-2', 'Playing',         '@x',     '0'),
    (306, 'Stopped', '@N',   '@m',  '0',  'on', 'SeekIndex'         ,    '@N-1',    '@N-1', 'Playing',       '@N-1',     '0'),
    (307, 'Stopped', '@N',   '@m',  '0',  'on', 'SeekIndex'         ,      '@N',        '', 'Stopped',          '0',     '0'),
    (308, 'Stopped', '@N', '@N-1',  '0',  'on', 'SeekIndex'         ,       '0',       '0', 'Playing',          '0',     '0'),
    (309, 'Stopped', '@N', '@N-1',  '0',  'on', 'SeekIndex'         ,       '1',    '@N-2', 'Playing',         '@x',     '0'),
    (310, 'Stopped', '@N', '@N-1',  '0',  'on', 'SeekIndex'         ,    '@N-1',    '@N-1', 'Playing',       '@N-1',     '0'),
    (311, 'Stopped', '@N', '@N-1',  '0',  'on', 'SeekIndex'         ,      '@N',        '', 'Stopped',          '0',     '0'),

    (321, 'Stopped', '@N',    '0',  '0', 'off', 'Next'              ,        '',        '', 'Playing',          '1',     '0'),
    (322, 'Stopped', '@N',    '0',  '0', 'off', 'Previous'          ,        '',        '', 'Stopped',          '0',     '0'),
    (330, 'Stopped', '@N',   '@m',  '0', 'off', 'Next'              ,        '',        '', 'Playing',       '@m+1',     '0'),
    (331, 'Stopped', '@N',   '@m',  '0', 'off', 'Previous'          ,        '',        '', 'Playing',       '@m-1',     '0'),
    (339, 'Stopped', '@N', '@N-1',  '0', 'off', 'Next'              ,        '',        '', 'Stopped',          '0',     '0'),
    (340, 'Stopped', '@N', '@N-1',  '0', 'off', 'Previous'          ,        '',        '', 'Playing',       '@N-2',     '0'),

    (351, 'Stopped', '@N',    '0',  '0',  'on', 'Next'              ,        '',        '', 'Playing',          '1',     '0'),
    (352, 'Stopped', '@N',    '0',  '0',  'on', 'Previous'          ,        '',        '', 'Playing',       '@N-1',     '0'),
    (360, 'Stopped', '@N',   '@m',  '0',  'on', 'Next'              ,        '',        '', 'Playing',       '@m+1',     '0'),
    (361, 'Stopped', '@N',   '@m',  '0',  'on', 'Previous'          ,        '',        '', 'Playing',       '@m-1',     '0'),
    (369, 'Stopped', '@N', '@N-1',  '0',  'on', 'Next'              ,        '',        '', 'Playing',          '0',     '0'),
    (370, 'Stopped', '@N', '@N-1',  '0',  'on', 'Previous'          ,        '',        '', 'Playing',       '@N-2',     '0'),

    
    #   | Preconditions                       | Stimulus                                  | Outcome
    # Id| State     PlLen  Track   Secs   Rpt | Action                MinVal     MaxVal   | State      Track         Secs
    (400, 'Playing', '@N',    '0', '@T', 'off', 'Play'              ,        '',        '', 'Playing',          '0',     '0'),
    (401, 'Playing', '@N',   '@m', '@T', 'off', 'Play'              ,        '',        '', 'Playing',         '@m',     '0'),
    (402, 'Playing', '@N', '@N-1', '@T', 'off', 'Play'              ,        '',        '', 'Playing',       '@N-1',     '0'),
    
    (410, 'Playing', '@N',    '0', '@T', 'off', 'Pause'             ,        '',        '', 'Paused' ,          '0',    '@T'),
    (411, 'Playing', '@N',   '@m', '@T', 'off', 'Pause'             ,        '',        '', 'Paused' ,         '@m',    '@T'),
    (412, 'Playing', '@N', '@N-1', '@T', 'off', 'Pause'             ,        '',        '', 'Paused' ,       '@N-1',    '@T'),
    
    (420, 'Playing', '@N',    '0', '@T', 'off', 'Stop'              ,        '',        '', 'Stopped',          '0',     '0'),
    (421, 'Playing', '@N',   '@m', '@T', 'off', 'Stop'              ,        '',        '', 'Stopped',         '@m',     '0'),
    (422, 'Playing', '@N', '@N-1', '@T', 'off', 'Stop'              ,        '',        '', 'Stopped',       '@N-1',     '0'),
    
    (430, 'Playing', '@N',    '0', '@T', 'off', 'SeekSecondAbsolute',      '@T',   '@D-10', 'Playing',          '0',    '@y'),
    (431, 'Playing', '@N',    '0', '@T', 'off', 'SeekSecondAbsolute',       '0',      '@T', 'Playing',          '0',    '@y'),
    (432, 'Playing', '@N',    '0', '@T', 'off', 'SeekSecondAbsolute',      '@D',      '@D', 'Playing',          '1',     '0'),
    (433, 'Playing', '@N',    '0', '@T', 'off', 'SeekSecondAbsolute',       '0',       '0', 'Playing',          '0',     '0'),
    (434, 'Playing', '@N',    '0', '@T', 'off', 'SeekSecondAbsolute',    '@D+1',        '', 'Playing',          '1',     '0'),
    (435, 'Playing', '@N',   '@m', '@T', 'off', 'SeekSecondAbsolute',      '@T',   '@D-10', 'Playing',         '@m',    '@y'),
    (436, 'Playing', '@N',   '@m', '@T', 'off', 'SeekSecondAbsolute',       '0',      '@T', 'Playing',         '@m',    '@y'),
    (437, 'Playing', '@N',   '@m', '@T', 'off', 'SeekSecondAbsolute',      '@D',      '@D', 'Playing',       '@m+1',     '0'),
    (438, 'Playing', '@N',   '@m', '@T', 'off', 'SeekSecondAbsolute',       '0',       '0', 'Playing',         '@m',     '0'),
    (439, 'Playing', '@N',   '@m', '@T', 'off', 'SeekSecondAbsolute',    '@D+1',        '', 'Playing',       '@m+1',     '0'),
    (440, 'Playing', '@N', '@N-1', '@T', 'off', 'SeekSecondAbsolute',      '@T',   '@D-10', 'Playing',       '@N-1',    '@y'),
    (441, 'Playing', '@N', '@N-1', '@T', 'off', 'SeekSecondAbsolute',       '0',      '@T', 'Playing',       '@N-1',    '@y'),
    (442, 'Playing', '@N', '@N-1', '@T', 'off', 'SeekSecondAbsolute',      '@D',      '@D', 'Stopped',          '0',     '0'),
    (443, 'Playing', '@N', '@N-1', '@T', 'off', 'SeekSecondAbsolute',       '0',       '0', 'Playing',       '@N-1',     '0'),
    (444, 'Playing', '@N', '@N-1', '@T', 'off', 'SeekSecondAbsolute',    '@D+1',        '', 'Stopped',         '0',     '0'),
     
    (450, 'Playing', '@N',    '0', '@T',  'on', 'SeekSecondAbsolute',      '@T',   '@D-10', 'Playing',          '0',    '@y'),
    (451, 'Playing', '@N',    '0', '@T',  'on', 'SeekSecondAbsolute',       '0',      '@T', 'Playing',          '0',    '@y'),
    (452, 'Playing', '@N',    '0', '@T',  'on', 'SeekSecondAbsolute',      '@D',      '@D', 'Playing',          '1',     '0'),
    (453, 'Playing', '@N',    '0', '@T',  'on', 'SeekSecondAbsolute',       '0',       '0', 'Playing',          '0',     '0'),
    (454, 'Playing', '@N',    '0', '@T',  'on', 'SeekSecondAbsolute',    '@D+1',        '', 'Playing',          '1',     '0'),
    (455, 'Playing', '@N',   '@m', '@T',  'on', 'SeekSecondAbsolute',      '@T',   '@D-10', 'Playing',         '@m',    '@y'),
    (456, 'Playing', '@N',   '@m', '@T',  'on', 'SeekSecondAbsolute',       '0',      '@T', 'Playing',         '@m',    '@y'),
    (457, 'Playing', '@N',   '@m', '@T',  'on', 'SeekSecondAbsolute',      '@D',      '@D', 'Playing',       '@m+1',     '0'),
    (458, 'Playing', '@N',   '@m', '@T',  'on', 'SeekSecondAbsolute',       '0',       '0', 'Playing',         '@m',     '0'),
    (459, 'Playing', '@N',   '@m', '@T',  'on', 'SeekSecondAbsolute',    '@D+1',        '', 'Playing',       '@m+1',     '0'),
    (460, 'Playing', '@N', '@N-1', '@T',  'on', 'SeekSecondAbsolute',      '@T',   '@D-10', 'Playing',       '@N-1',    '@y'),
    (461, 'Playing', '@N', '@N-1', '@T',  'on', 'SeekSecondAbsolute',       '0',      '@T', 'Playing',       '@N-1',    '@y'),
    (462, 'Playing', '@N', '@N-1', '@T',  'on', 'SeekSecondAbsolute',      '@D',      '@D', 'Playing',          '0',     '0'),
    (463, 'Playing', '@N', '@N-1', '@T',  'on', 'SeekSecondAbsolute',       '0',       '0', 'Playing',       '@N-1',     '0'),
    (464, 'Playing', '@N', '@N-1', '@T',  'on', 'SeekSecondAbsolute',    '@D+1',        '', 'Playing',          '0',     '0'),

    (470, 'Playing', '@N',    '0', '@T', 'off', 'SeekSecondRelative',       '0',       '0', 'Playing',          '0',    '@T'),
    (471, 'Playing', '@N',    '0', '@T', 'off', 'SeekSecondRelative',        '','@D-@T-10', 'Playing',          '0', '@T+@y'),
    (472, 'Playing', '@N',    '0', '@T', 'off', 'SeekSecondRelative',   '-@T+1',      '-1', 'Playing',          '0', '@T+@y'),
    (473, 'Playing', '@N',    '0', '@T', 'off', 'SeekSecondRelative',   '@D-@T',   '@D-@T', 'Playing',          '1',     '0'),
    (474, 'Playing', '@N',    '0', '@T', 'off', 'SeekSecondRelative',     '-@T',     '-@T', 'Playing',          '0',     '0'),
    (475, 'Playing', '@N',    '0', '@T', 'off', 'SeekSecondRelative', '@D-@T+1',        '', 'Playing',          '1',     '0'),
    (476, 'Playing', '@N',    '0', '@T', 'off', 'SeekSecondRelative',        '',   '-@T-1', 'Playing',          '0',     '0'),
    (477, 'Playing', '@N',   '@m', '@T', 'off', 'SeekSecondRelative',       '0',       '0', 'Playing',         '@m',    '@T'),
    (478, 'Playing', '@N',   '@m', '@T', 'off', 'SeekSecondRelative',        '','@D-@T-10', 'Playing',         '@m', '@T+@y'),
    (479, 'Playing', '@N',   '@m', '@T', 'off', 'SeekSecondRelative',   '-@T+1',      '-1', 'Playing',         '@m', '@T+@y'),
    (480, 'Playing', '@N',   '@m', '@T', 'off', 'SeekSecondRelative',   '@D-@T',   '@D-@T', 'Playing',       '@m+1',     '0'),
    (481, 'Playing', '@N',   '@m', '@T', 'off', 'SeekSecondRelative',     '-@T',     '-@T', 'Playing',         '@m',     '0'),
    (482, 'Playing', '@N',   '@m', '@T', 'off', 'SeekSecondRelative', '@D-@T+1',        '', 'Playing',       '@m+1',     '0'),
    (483, 'Playing', '@N',   '@m', '@T', 'off', 'SeekSecondRelative',        '',   '-@T-1', 'Playing',         '@m',     '0'),
    (484, 'Playing', '@N', '@N-1', '@T', 'off', 'SeekSecondRelative',       '0',       '0', 'Playing',       '@N-1',    '@T'),
    (485, 'Playing', '@N', '@N-1', '@T', 'off', 'SeekSecondRelative',        '','@D-@T-10', 'Playing',       '@N-1', '@T+@y'),
    (486, 'Playing', '@N', '@N-1', '@T', 'off', 'SeekSecondRelative',   '-@T+1',      '-1', 'Playing',       '@N-1', '@T+@y'),
    (487, 'Playing', '@N', '@N-1', '@T', 'off', 'SeekSecondRelative',   '@D-@T',   '@D-@T', 'Stopped',         '0',     '0'),
    (488, 'Playing', '@N', '@N-1', '@T', 'off', 'SeekSecondRelative',     '-@T',     '-@T', 'Playing',       '@N-1',     '0'),
    (489, 'Playing', '@N', '@N-1', '@T', 'off', 'SeekSecondRelative', '@D-@T+1',        '', 'Stopped',         '0',     '0'),
    (490, 'Playing', '@N', '@N-1', '@T', 'off', 'SeekSecondRelative',        '',   '-@T-1', 'Playing',       '@N-1',     '0'),
     
    (500, 'Playing', '@N',    '0', '@T',  'on', 'SeekSecondRelative',       '0',       '0', 'Playing',          '0',    '@T'),
    (501, 'Playing', '@N',    '0', '@T',  'on', 'SeekSecondRelative',        '','@D-@T-10', 'Playing',          '0', '@T+@y'),
    (502, 'Playing', '@N',    '0', '@T',  'on', 'SeekSecondRelative',   '-@T+1',      '-1', 'Playing',          '0', '@T+@y'),
    (503, 'Playing', '@N',    '0', '@T',  'on', 'SeekSecondRelative',   '@D-@T',   '@D-@T', 'Playing',          '1',     '0'),
    (504, 'Playing', '@N',    '0', '@T',  'on', 'SeekSecondRelative',     '-@T',     '-@T', 'Playing',          '0',     '0'),
    (505, 'Playing', '@N',    '0', '@T',  'on', 'SeekSecondRelative', '@D-@T+1',        '', 'Playing',          '1',     '0'),
    (506, 'Playing', '@N',    '0', '@T',  'on', 'SeekSecondRelative',        '',   '-@T-1', 'Playing',          '0',     '0'),
    (507, 'Playing', '@N',   '@m', '@T',  'on', 'SeekSecondRelative',       '0',       '0', 'Playing',         '@m',    '@T'),
    (508, 'Playing', '@N',   '@m', '@T',  'on', 'SeekSecondRelative',        '','@D-@T-10', 'Playing',         '@m', '@T+@y'),
    (509, 'Playing', '@N',   '@m', '@T',  'on', 'SeekSecondRelative',   '-@T+1',      '-1', 'Playing',         '@m', '@T+@y'),
    (510, 'Playing', '@N',   '@m', '@T',  'on', 'SeekSecondRelative',   '@D-@T',   '@D-@T', 'Playing',       '@m+1',     '0'),
    (511, 'Playing', '@N',   '@m', '@T',  'on', 'SeekSecondRelative',     '-@T',     '-@T', 'Playing',         '@m',     '0'),
    (512, 'Playing', '@N',   '@m', '@T',  'on', 'SeekSecondRelative', '@D-@T+1',        '', 'Playing',       '@m+1',     '0'),
    (513, 'Playing', '@N',   '@m', '@T',  'on', 'SeekSecondRelative',        '',   '-@T-1', 'Playing',         '@m',     '0'),
    (514, 'Playing', '@N', '@N-1', '@T',  'on', 'SeekSecondRelative',       '0',       '0', 'Playing',       '@N-1',    '@T'),
    (515, 'Playing', '@N', '@N-1', '@T',  'on', 'SeekSecondRelative',        '','@D-@T-10', 'Playing',       '@N-1', '@T+@y'),
    (516, 'Playing', '@N', '@N-1', '@T',  'on', 'SeekSecondRelative',   '-@T+1',      '-1', 'Playing',       '@N-1', '@T+@y'),
    (517, 'Playing', '@N', '@N-1', '@T',  'on', 'SeekSecondRelative',   '@D-@T',   '@D-@T', 'Playing',          '0',     '0'),
    (518, 'Playing', '@N', '@N-1', '@T',  'on', 'SeekSecondRelative',     '-@T',     '-@T', 'Playing',       '@N-1',     '0'),
    (519, 'Playing', '@N', '@N-1', '@T',  'on', 'SeekSecondRelative', '@D-@T+1',        '', 'Playing',          '0',     '0'),
    (520, 'Playing', '@N', '@N-1', '@T',  'on', 'SeekSecondRelative',        '',   '-@T-1', 'Playing',       '@N-1',     '0'),
     
    (530, 'Playing', '@N',    '0', '@T', 'off', 'SeekIndex'         ,       '0',       '0', 'Playing',          '0',     '0'),
    (531, 'Playing', '@N',    '0', '@T', 'off', 'SeekIndex'         ,       '1',    '@N-2', 'Playing',         '@x',     '0'),
    (532, 'Playing', '@N',    '0', '@T', 'off', 'SeekIndex'         ,    '@N-1',    '@N-1', 'Playing',       '@N-1',     '0'),
    (533, 'Playing', '@N',    '0', '@T', 'off', 'SeekIndex'         ,      '@N',        '', 'Stopped',         '0',     '0'),
    (534, 'Playing', '@N',   '@m', '@T', 'off', 'SeekIndex'         ,       '0',       '0', 'Playing',          '0',     '0'),
    (535, 'Playing', '@N',   '@m', '@T', 'off', 'SeekIndex'         ,       '1',    '@N-2', 'Playing',         '@x',     '0'),
    (536, 'Playing', '@N',   '@m', '@T', 'off', 'SeekIndex'         ,    '@N-1',    '@N-1', 'Playing',       '@N-1',     '0'),
    (537, 'Playing', '@N',   '@m', '@T', 'off', 'SeekIndex'         ,      '@N',        '', 'Stopped',         '0',     '0'),
    (538, 'Playing', '@N', '@N-1', '@T', 'off', 'SeekIndex'         ,       '0',       '0', 'Playing',          '0',     '0'),
    (539, 'Playing', '@N', '@N-1', '@T', 'off', 'SeekIndex'         ,       '1',    '@N-2', 'Playing',         '@x',     '0'),
    (540, 'Playing', '@N', '@N-1', '@T', 'off', 'SeekIndex'         ,    '@N-1',    '@N-1', 'Playing',       '@N-1',     '0'),
    (541, 'Playing', '@N', '@N-1', '@T', 'off', 'SeekIndex'         ,      '@N',        '', 'Stopped',         '0',     '0'),
     
    (550, 'Playing', '@N',    '0', '@T',  'on', 'SeekIndex'         ,       '0',       '0', 'Playing',          '0',     '0'),
    (551, 'Playing', '@N',    '0', '@T',  'on', 'SeekIndex'         ,       '1',    '@N-2', 'Playing',         '@x',     '0'),
    (552, 'Playing', '@N',    '0', '@T',  'on', 'SeekIndex'         ,    '@N-1',    '@N-1', 'Playing',       '@N-1',     '0'),
    (553, 'Playing', '@N',    '0', '@T',  'on', 'SeekIndex'         ,      '@N',        '', 'Stopped',         '0',     '0'),
    (554, 'Playing', '@N',   '@m', '@T',  'on', 'SeekIndex'         ,       '0',       '0', 'Playing',          '0',     '0'),
    (555, 'Playing', '@N',   '@m', '@T',  'on', 'SeekIndex'         ,       '1',    '@N-2', 'Playing',         '@x',     '0'),
    (556, 'Playing', '@N',   '@m', '@T',  'on', 'SeekIndex'         ,    '@N-1',    '@N-1', 'Playing',       '@N-1',     '0'),
    (557, 'Playing', '@N',   '@m', '@T',  'on', 'SeekIndex'         ,      '@N',        '', 'Stopped',         '0',     '0'),
    (558, 'Playing', '@N', '@N-1', '@T',  'on', 'SeekIndex'         ,       '0',       '0', 'Playing',          '0',     '0'),
    (559, 'Playing', '@N', '@N-1', '@T',  'on', 'SeekIndex'         ,       '1',    '@N-2', 'Playing',         '@x',     '0'),
    (560, 'Playing', '@N', '@N-1', '@T',  'on', 'SeekIndex'         ,    '@N-1',    '@N-1', 'Playing',       '@N-1',     '0'),
    (561, 'Playing', '@N', '@N-1', '@T',  'on', 'SeekIndex'         ,      '@N',        '', 'Stopped',         '0',     '0'),
     
    (571, 'Playing', '@N',    '0', '@T', 'off', 'Next'              ,        '',        '', 'Playing',          '1',     '0'),
    (572, 'Playing', '@N',    '0', '@T', 'off', 'Previous'          ,        '',        '', 'Stopped',         '0',     '0'),
    (580, 'Playing', '@N',   '@m', '@T', 'off', 'Next'              ,        '',        '', 'Playing',       '@m+1',     '0'),
    (581, 'Playing', '@N',   '@m', '@T', 'off', 'Previous'          ,        '',        '', 'Playing',       '@m-1',     '0'),
    (589, 'Playing', '@N', '@N-1', '@T', 'off', 'Next'              ,        '',        '', 'Stopped',         '0',     '0'),
    (590, 'Playing', '@N', '@N-1', '@T', 'off', 'Previous'          ,        '',        '', 'Playing',       '@N-2',     '0'),
     
    (601, 'Playing', '@N',    '0', '@T',  'on', 'Next'              ,        '',        '', 'Playing',          '1',     '0'),
    (602, 'Playing', '@N',    '0', '@T',  'on', 'Previous'          ,        '',        '', 'Playing',       '@N-1',     '0'),
    (610, 'Playing', '@N',   '@m', '@T',  'on', 'Next'              ,        '',        '', 'Playing',       '@m+1',     '0'),
    (611, 'Playing', '@N',   '@m', '@T',  'on', 'Previous'          ,        '',        '', 'Playing',       '@m-1',     '0'),
    (619, 'Playing', '@N', '@N-1', '@T',  'on', 'Next'              ,        '',        '', 'Playing',          '0',     '0'),
    (620, 'Playing', '@N', '@N-1', '@T',  'on', 'Previous'          ,        '',        '', 'Playing',       '@N-2',     '0'),
 
    
#   | Preconditions                       | Stimulus                                  | Outcome
    # Id| State     PlLen  Track   Secs   Rpt | Action                MinVal     MaxVal   | State      Track         Secs
    (700, 'Paused' , '@N',    '0', '@T', 'off', 'Play'              ,        '',        '', 'Playing',          '0',    '@T'),
    (701, 'Paused' , '@N',   '@m', '@T', 'off', 'Play'              ,        '',        '', 'Playing',         '@m',    '@T'),
    (702, 'Paused' , '@N', '@N-1', '@T', 'off', 'Play'              ,        '',        '', 'Playing',       '@N-1',    '@T'),
    
    (710, 'Paused' , '@N',    '0', '@T', 'off', 'Pause'             ,        '',        '', 'Paused' ,          '0',    '@T'),
    (711, 'Paused' , '@N',   '@m', '@T', 'off', 'Pause'             ,        '',        '', 'Paused' ,         '@m',    '@T'),
    (712, 'Paused' , '@N', '@N-1', '@T', 'off', 'Pause'             ,        '',        '', 'Paused' ,       '@N-1',    '@T'),
    
    (720, 'Paused' , '@N',    '0', '@T', 'off', 'Stop'              ,        '',        '', 'Stopped',          '0',     '0'),
    (721, 'Paused' , '@N',   '@m', '@T', 'off', 'Stop'              ,        '',        '', 'Stopped',         '@m',     '0'),
    (722, 'Paused' , '@N', '@N-1', '@T', 'off', 'Stop'              ,        '',        '', 'Stopped',       '@N-1',     '0'),
    
    (730, 'Paused' , '@N',    '0', '@T', 'off', 'SeekSecondAbsolute',      '@T',   '@D-10', 'Playing',          '0',    '@y'),
    (731, 'Paused' , '@N',    '0', '@T', 'off', 'SeekSecondAbsolute',       '0',      '@T', 'Playing',          '0',    '@y'),
    (732, 'Paused' , '@N',    '0', '@T', 'off', 'SeekSecondAbsolute',      '@D',      '@D', 'Playing',          '1',     '0'),
    (733, 'Paused' , '@N',    '0', '@T', 'off', 'SeekSecondAbsolute',       '0',       '0', 'Playing',          '0',     '0'),
    (734, 'Paused' , '@N',    '0', '@T', 'off', 'SeekSecondAbsolute',    '@D+1',        '', 'Playing',          '1',     '0'),
    (735, 'Paused' , '@N',   '@m', '@T', 'off', 'SeekSecondAbsolute',      '@T',   '@D-10', 'Playing',         '@m',    '@y'),
    (736, 'Paused' , '@N',   '@m', '@T', 'off', 'SeekSecondAbsolute',       '0',      '@T', 'Playing',         '@m',    '@y'),
    (737, 'Paused' , '@N',   '@m', '@T', 'off', 'SeekSecondAbsolute',      '@D',      '@D', 'Playing',       '@m+1',     '0'),
    (738, 'Paused' , '@N',   '@m', '@T', 'off', 'SeekSecondAbsolute',       '0',       '0', 'Playing',         '@m',     '0'),
    (739, 'Paused' , '@N',   '@m', '@T', 'off', 'SeekSecondAbsolute',    '@D+1',        '', 'Playing',       '@m+1',     '0'),
    (740, 'Paused' , '@N', '@N-1', '@T', 'off', 'SeekSecondAbsolute',      '@T',   '@D-10', 'Playing',       '@N-1',    '@y'),
    (741, 'Paused' , '@N', '@N-1', '@T', 'off', 'SeekSecondAbsolute',       '0',      '@T', 'Playing',       '@N-1',    '@y'),
    (742, 'Paused' , '@N', '@N-1', '@T', 'off', 'SeekSecondAbsolute',      '@D',      '@D', 'Stopped',         '0',     '0'),
    (743, 'Paused' , '@N', '@N-1', '@T', 'off', 'SeekSecondAbsolute',       '0',       '0', 'Playing',       '@N-1',     '0'),
    (744, 'Paused' , '@N', '@N-1', '@T', 'off', 'SeekSecondAbsolute',    '@D+1',        '', 'Stopped',         '0',     '0'),
     
    (750, 'Paused' , '@N',    '0', '@T',  'on', 'SeekSecondAbsolute',      '@T',   '@D-10', 'Playing',          '0',    '@y'),
    (751, 'Paused' , '@N',    '0', '@T',  'on', 'SeekSecondAbsolute',       '0',      '@T', 'Playing',          '0',    '@y'),
    (752, 'Paused' , '@N',    '0', '@T',  'on', 'SeekSecondAbsolute',      '@D',      '@D', 'Playing',          '1',     '0'),
    (753, 'Paused' , '@N',    '0', '@T',  'on', 'SeekSecondAbsolute',       '0',       '0', 'Playing',          '0',     '0'),
    (754, 'Paused' , '@N',    '0', '@T',  'on', 'SeekSecondAbsolute',    '@D+1',        '', 'Playing',          '1',     '0'),
    (755, 'Paused' , '@N',   '@m', '@T',  'on', 'SeekSecondAbsolute',      '@T',   '@D-10', 'Playing',         '@m',    '@y'),
    (756, 'Paused' , '@N',   '@m', '@T',  'on', 'SeekSecondAbsolute',       '0',      '@T', 'Playing',         '@m',    '@y'),
    (757, 'Paused' , '@N',   '@m', '@T',  'on', 'SeekSecondAbsolute',      '@D',      '@D', 'Playing',       '@m+1',     '0'),
    (758, 'Paused' , '@N',   '@m', '@T',  'on', 'SeekSecondAbsolute',       '0',       '0', 'Playing',         '@m',     '0'),
    (759, 'Paused' , '@N',   '@m', '@T',  'on', 'SeekSecondAbsolute',    '@D+1',        '', 'Playing',       '@m+1',     '0'),
    (760, 'Paused' , '@N', '@N-1', '@T',  'on', 'SeekSecondAbsolute',      '@T',   '@D-10', 'Playing',       '@N-1',    '@y'),
    (761, 'Paused' , '@N', '@N-1', '@T',  'on', 'SeekSecondAbsolute',       '0',      '@T', 'Playing',       '@N-1',    '@y'),
    (762, 'Paused' , '@N', '@N-1', '@T',  'on', 'SeekSecondAbsolute',      '@D',      '@D', 'Playing',          '0',     '0'),
    (763, 'Paused' , '@N', '@N-1', '@T',  'on', 'SeekSecondAbsolute',       '0',       '0', 'Playing',       '@N-1',     '0'),
    (764, 'Paused' , '@N', '@N-1', '@T',  'on', 'SeekSecondAbsolute',    '@D+1',        '', 'Playing',          '0',     '0'),

    (770, 'Paused' , '@N',    '0', '@T', 'off', 'SeekSecondRelative',       '0',       '0', 'Playing',          '0',    '@T'),
    (771, 'Paused' , '@N',    '0', '@T', 'off', 'SeekSecondRelative',        '','@D-@T-10', 'Playing',          '0', '@T+@y'),
    (772, 'Paused' , '@N',    '0', '@T', 'off', 'SeekSecondRelative',   '-@T+1',      '-1', 'Playing',          '0', '@T+@y'),
    (773, 'Paused' , '@N',    '0', '@T', 'off', 'SeekSecondRelative',   '@D-@T',   '@D-@T', 'Playing',          '1',     '0'),
    (774, 'Paused' , '@N',    '0', '@T', 'off', 'SeekSecondRelative',     '-@T',     '-@T', 'Playing',          '0',     '0'),
    (775, 'Paused' , '@N',    '0', '@T', 'off', 'SeekSecondRelative', '@D-@T+1',        '', 'Playing',          '1',     '0'),
    (776, 'Paused' , '@N',    '0', '@T', 'off', 'SeekSecondRelative',        '',   '-@T-1', 'Playing',          '0',     '0'),
    (777, 'Paused' , '@N',   '@m', '@T', 'off', 'SeekSecondRelative',       '0',       '0', 'Playing',         '@m',    '@T'),
    (778, 'Paused' , '@N',   '@m', '@T', 'off', 'SeekSecondRelative',        '','@D-@T-10', 'Playing',         '@m', '@T+@y'),
    (779, 'Paused' , '@N',   '@m', '@T', 'off', 'SeekSecondRelative',   '-@T+1',      '-1', 'Playing',         '@m', '@T+@y'),
    (780, 'Paused' , '@N',   '@m', '@T', 'off', 'SeekSecondRelative',   '@D-@T',   '@D-@T', 'Playing',       '@m+1',     '0'),
    (781, 'Paused' , '@N',   '@m', '@T', 'off', 'SeekSecondRelative',     '-@T',     '-@T', 'Playing',         '@m',     '0'),
    (782, 'Paused' , '@N',   '@m', '@T', 'off', 'SeekSecondRelative', '@D-@T+1',        '', 'Playing',       '@m+1',     '0'),
    (783, 'Paused' , '@N',   '@m', '@T', 'off', 'SeekSecondRelative',        '',   '-@T-1', 'Playing',         '@m',     '0'),
    (784, 'Paused' , '@N', '@N-1', '@T', 'off', 'SeekSecondRelative',       '0',       '0', 'Playing',       '@N-1',    '@T'),
    (785, 'Paused' , '@N', '@N-1', '@T', 'off', 'SeekSecondRelative',        '','@D-@T-10', 'Playing',       '@N-1', '@T+@y'),
    (786, 'Paused' , '@N', '@N-1', '@T', 'off', 'SeekSecondRelative',   '-@T+1',      '-1', 'Playing',       '@N-1', '@T+@y'),
    (787, 'Paused' , '@N', '@N-1', '@T', 'off', 'SeekSecondRelative',   '@D-@T',   '@D-@T', 'Stopped',         '0',     '0'),
    (788, 'Paused' , '@N', '@N-1', '@T', 'off', 'SeekSecondRelative',     '-@T',     '-@T', 'Playing',       '@N-1',     '0'),
    (789, 'Paused' , '@N', '@N-1', '@T', 'off', 'SeekSecondRelative', '@D-@T+1',        '', 'Stopped',         '0',     '0'),
    (790, 'Paused' , '@N', '@N-1', '@T', 'off', 'SeekSecondRelative',        '',   '-@T-1', 'Playing',       '@N-1',     '0'),
     
    (800, 'Paused' , '@N',    '0', '@T',  'on', 'SeekSecondRelative',       '0',       '0', 'Playing',          '0',    '@T'),
    (801, 'Paused' , '@N',    '0', '@T',  'on', 'SeekSecondRelative',        '','@D-@T-10', 'Playing',          '0', '@T+@y'),
    (802, 'Paused' , '@N',    '0', '@T',  'on', 'SeekSecondRelative',   '-@T+1',      '-1', 'Playing',          '0', '@T+@y'),
    (803, 'Paused' , '@N',    '0', '@T',  'on', 'SeekSecondRelative',   '@D-@T',   '@D-@T', 'Playing',          '1',     '0'),
    (804, 'Paused' , '@N',    '0', '@T',  'on', 'SeekSecondRelative',     '-@T',     '-@T', 'Playing',          '0',     '0'),
    (805, 'Paused' , '@N',    '0', '@T',  'on', 'SeekSecondRelative', '@D-@T+1',        '', 'Playing',          '1',     '0'),
    (806, 'Paused' , '@N',    '0', '@T',  'on', 'SeekSecondRelative',        '',   '-@T-1', 'Playing',          '0',     '0'),
    (807, 'Paused' , '@N',   '@m', '@T',  'on', 'SeekSecondRelative',       '0',       '0', 'Playing',         '@m',    '@T'),
    (808, 'Paused' , '@N',   '@m', '@T',  'on', 'SeekSecondRelative',        '','@D-@T-10', 'Playing',         '@m', '@T+@y'),
    (809, 'Paused' , '@N',   '@m', '@T',  'on', 'SeekSecondRelative',   '-@T+1',      '-1', 'Playing',         '@m', '@T+@y'),
    (810, 'Paused' , '@N',   '@m', '@T',  'on', 'SeekSecondRelative',   '@D-@T',   '@D-@T', 'Playing',       '@m+1',     '0'),
    (811, 'Paused' , '@N',   '@m', '@T',  'on', 'SeekSecondRelative',     '-@T',     '-@T', 'Playing',         '@m',     '0'),
    (812, 'Paused' , '@N',   '@m', '@T',  'on', 'SeekSecondRelative', '@D-@T+1',        '', 'Playing',       '@m+1',     '0'),
    (813, 'Paused' , '@N',   '@m', '@T',  'on', 'SeekSecondRelative',        '',   '-@T-1', 'Playing',         '@m',     '0'),
    (814, 'Paused' , '@N', '@N-1', '@T',  'on', 'SeekSecondRelative',       '0',       '0', 'Playing',       '@N-1',    '@T'),
    (815, 'Paused' , '@N', '@N-1', '@T',  'on', 'SeekSecondRelative',        '','@D-@T-10', 'Playing',       '@N-1', '@T+@y'),
    (816, 'Paused' , '@N', '@N-1', '@T',  'on', 'SeekSecondRelative',   '-@T+1',      '-1', 'Playing',       '@N-1', '@T+@y'),
    (817, 'Paused' , '@N', '@N-1', '@T',  'on', 'SeekSecondRelative',   '@D-@T',   '@D-@T', 'Playing',          '0',     '0'),
    (818, 'Paused' , '@N', '@N-1', '@T',  'on', 'SeekSecondRelative',     '-@T',     '-@T', 'Playing',       '@N-1',     '0'),
    (819, 'Paused' , '@N', '@N-1', '@T',  'on', 'SeekSecondRelative', '@D-@T+1',        '', 'Playing',          '0',     '0'),
    (820, 'Paused' , '@N', '@N-1', '@T',  'on', 'SeekSecondRelative',        '',   '-@T-1', 'Playing',       '@N-1',     '0'),
     
    (830, 'Paused' , '@N',    '0', '@T', 'off', 'SeekIndex'         ,       '0',       '0', 'Playing',          '0',     '0'),
    (831, 'Paused' , '@N',    '0', '@T', 'off', 'SeekIndex'         ,       '1',    '@N-2', 'Playing',         '@x',     '0'),
    (832, 'Paused' , '@N',    '0', '@T', 'off', 'SeekIndex'         ,    '@N-1',    '@N-1', 'Playing',       '@N-1',     '0'),
    (833, 'Paused' , '@N',    '0', '@T', 'off', 'SeekIndex'         ,      '@N',        '', 'Stopped',         '0',     '0'),
    (834, 'Paused' , '@N',   '@m', '@T', 'off', 'SeekIndex'         ,       '0',       '0', 'Playing',          '0',     '0'),
    (835, 'Paused' , '@N',   '@m', '@T', 'off', 'SeekIndex'         ,       '1',    '@N-2', 'Playing',         '@x',     '0'),
    (836, 'Paused' , '@N',   '@m', '@T', 'off', 'SeekIndex'         ,    '@N-1',    '@N-1', 'Playing',       '@N-1',     '0'),
    (837, 'Paused' , '@N',   '@m', '@T', 'off', 'SeekIndex'         ,      '@N',        '', 'Stopped',         '0',     '0'),
    (838, 'Paused' , '@N', '@N-1', '@T', 'off', 'SeekIndex'         ,       '0',       '0', 'Playing',          '0',     '0'),
    (839, 'Paused' , '@N', '@N-1', '@T', 'off', 'SeekIndex'         ,       '1',    '@N-2', 'Playing',         '@x',     '0'),
    (840, 'Paused' , '@N', '@N-1', '@T', 'off', 'SeekIndex'         ,    '@N-1',    '@N-1', 'Playing',       '@N-1',     '0'),
    (841, 'Paused' , '@N', '@N-1', '@T', 'off', 'SeekIndex'         ,      '@N',        '', 'Stopped',         '0',     '0'),
     
    (850, 'Paused' , '@N',    '0', '@T',  'on', 'SeekIndex'         ,       '0',       '0', 'Playing',          '0',     '0'),
    (851, 'Paused' , '@N',    '0', '@T',  'on', 'SeekIndex'         ,       '1',    '@N-2', 'Playing',         '@x',     '0'),
    (852, 'Paused' , '@N',    '0', '@T',  'on', 'SeekIndex'         ,    '@N-1',    '@N-1', 'Playing',       '@N-1',     '0'),
    (853, 'Paused' , '@N',    '0', '@T',  'on', 'SeekIndex'         ,      '@N',        '', 'Stopped',         '0',     '0'),
    (854, 'Paused' , '@N',   '@m', '@T',  'on', 'SeekIndex'         ,       '0',       '0', 'Playing',          '0',     '0'),
    (855, 'Paused' , '@N',   '@m', '@T',  'on', 'SeekIndex'         ,       '1',    '@N-2', 'Playing',         '@x',     '0'),
    (856, 'Paused' , '@N',   '@m', '@T',  'on', 'SeekIndex'         ,    '@N-1',    '@N-1', 'Playing',       '@N-1',     '0'),
    (857, 'Paused' , '@N',   '@m', '@T',  'on', 'SeekIndex'         ,      '@N',        '', 'Stopped',         '0',     '0'),
    (858, 'Paused' , '@N', '@N-1', '@T',  'on', 'SeekIndex'         ,       '0',       '0', 'Playing',          '0',     '0'),
    (859, 'Paused' , '@N', '@N-1', '@T',  'on', 'SeekIndex'         ,       '1',    '@N-2', 'Playing',         '@x',     '0'),
    (860, 'Paused' , '@N', '@N-1', '@T',  'on', 'SeekIndex'         ,    '@N-1',    '@N-1', 'Playing',       '@N-1',     '0'),
    (861, 'Paused' , '@N', '@N-1', '@T',  'on', 'SeekIndex'         ,      '@N',        '', 'Stopped',         '0',     '0'),
     
    (871, 'Paused' , '@N',    '0', '@T', 'off', 'Next'              ,        '',        '', 'Playing',          '1',     '0'),
    (872, 'Paused' , '@N',    '0', '@T', 'off', 'Previous'          ,        '',        '', 'Stopped',         '0',     '0'),
    (880, 'Paused' , '@N',   '@m', '@T', 'off', 'Next'              ,        '',        '', 'Playing',       '@m+1',     '0'),
    (881, 'Paused' , '@N',   '@m', '@T', 'off', 'Previous'          ,        '',        '', 'Playing',       '@m-1',     '0'),
    (889, 'Paused' , '@N', '@N-1', '@T', 'off', 'Next'              ,        '',        '', 'Stopped',         '0',     '0'),
    (890, 'Paused' , '@N', '@N-1', '@T', 'off', 'Previous'          ,        '',        '', 'Playing',       '@N-2',     '0'),
     
    (901, 'Paused' , '@N',    '0', '@T',  'on', 'Next'              ,        '',        '', 'Playing',          '1',     '0'),
    (902, 'Paused' , '@N',    '0', '@T',  'on', 'Previous'          ,        '',        '', 'Playing',       '@N-1',     '0'),
    (910, 'Paused' , '@N',   '@m', '@T',  'on', 'Next'              ,        '',        '', 'Playing',       '@m+1',     '0'),
    (911, 'Paused' , '@N',   '@m', '@T',  'on', 'Previous'          ,        '',        '', 'Playing',       '@m-1',     '0'),
    (919, 'Paused' , '@N', '@N-1', '@T',  'on', 'Next'              ,        '',        '', 'Playing',          '0',     '0'),
    (920, 'Paused' , '@N', '@N-1', '@T',  'on', 'Previous'          ,        '',        '', 'Playing',       '@N-2',     '0')
    ]


if __name__ == '__main__':
    
    BASE.Run( sys.argv )
