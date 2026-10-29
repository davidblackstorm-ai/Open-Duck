#pragma once
// Offline audio fixtures. No device, file installation or DAW is opened.
#include "../Source/PluginProcessor.h"
#include <iostream>
#include <fstream>
#include <functional>
#include <vector>
#include <array>
struct Head:juce::AudioPlayHead{double ppq=0;juce::Optional<PositionInfo>getPosition()const override{PositionInfo p;p.setBpm(145);p.setPpqPosition(ppq);p.setIsPlaying(true);return p;}};
void set(OpenDuckProcessor&p,const juce::String&id,float v){auto*q=p.state.getParameter(id);if(!q)throw std::runtime_error(id.toStdString());q->setValueNotifyingHost(q->convertTo0to1(v));}
struct Audio{double rate=0;std::array<std::vector<float>,2>v;};
Audio load(const juce::File&file){juce::WavAudioFormat format;std::unique_ptr<juce::AudioFormatReader>r(format.createReaderFor(file.createInputStream().release(),true));if(!r)throw std::runtime_error("Cannot read WAV");Audio a;a.rate=r->sampleRate;juce::AudioBuffer<float>b(2,int(r->lengthInSamples));r->read(&b,0,b.getNumSamples(),0,true,true);for(int c=0;c<2;++c)a.v[c].assign(b.getReadPointer(c),b.getReadPointer(c)+b.getNumSamples());return a;}
void save(const juce::File&file,const Audio&a){file.deleteFile();juce::WavAudioFormat format;std::unique_ptr<juce::AudioFormatWriter>w(format.createWriterFor(file.createOutputStream().release(),a.rate,2,24,{},0));if(!w)throw std::runtime_error("Cannot write WAV");juce::AudioBuffer<float>b(2,int(a.v[0].size()));for(int c=0;c<2;++c)std::copy(a.v[c].begin(),a.v[c].end(),b.getWritePointer(c));if(!w->writeFromAudioSampleBuffer(b,0,b.getNumSamples()))throw std::runtime_error("WAV write failure");}
using Setup=std::function<void(OpenDuckProcessor&)>;
struct AuditRender{Audio wet;int latency=0;};
AuditRender render(const Audio&input,const Audio&sc,int mode,const Setup&setup){auto owner=std::make_unique<OpenDuckProcessor>();auto&p=*owner;Head h;p.setPlayHead(&h);auto layout=p.getBusesLayout();layout.inputBuses.set(1,juce::AudioChannelSet::stereo());if(!p.setBusesLayout(layout))throw std::runtime_error("Sidechain layout");for(int k=0;k<6;++k){set(p,"on"+juce::String(k),mode==-2||mode==k?1:0);set(p,"solo"+juce::String(k),0);}set(p,"mix",100);if(setup)setup(p);p.prepareToPlay(input.rate,257);AuditRender r;r.latency=p.getLatencySamples();r.wet.rate=input.rate;const int total=int(input.v[0].size())+r.latency+2048;for(auto&v:r.wet.v)v.resize(total);for(int start=0;start<total;start+=257){const int count=juce::jmin(257,total-start);juce::AudioBuffer<float>b(4,count);b.clear();for(int c=0;c<2;++c)for(int i=0;i<count;++i){const int at=start+i;if(at<int(input.v[c].size()))b.setSample(c,i,input.v[c][at]);if(at<int(sc.v[c].size()))b.setSample(c+2,i,sc.v[c][at]);}h.ppq=start/input.rate*145/60;juce::MidiBuffer midi;p.processBlock(b,midi);for(int c=0;c<2;++c)std::copy(b.getReadPointer(c),b.getReadPointer(c)+count,r.wet.v[c].begin()+start);}return r;}
OpenDuckProcessor::CurveSnapshot flat(float y){OpenDuckProcessor::CurveSnapshot s;s.custom=true;s.shape=16;s.count=2;s.points[0]={0,y};s.points[1]={1,y};return s;}
struct Stats{double in=0,out=0,error=0,peakIn=0,peakOut=0;long count=0;void add(double x,double y){in+=x*x;out+=y*y;error+=(x-y)*(x-y);peakIn=std::max(peakIn,std::abs(x));peakOut=std::max(peakOut,std::abs(y));++count;}double gain()const{return 10*std::log10(std::max(out,1e-30)/std::max(in,1e-30));}double nullRms()const{return std::sqrt(error/std::max(1L,count));}};
