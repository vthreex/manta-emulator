import json,subprocess,sys,os,threading
root='<repo-root>/'
base=json.load(open(root+'outputs/manta-emulator/configs/checkpoints/connected-menu-base.json'))['argv']
exe=root+'work/build-irq/manta_p6.exe'
snap=root+'work/checkpoints/no-signal-ready-v2.snap'
INJECT=1300000000
DIG={'0':0x45,'1':1,'2':2,'3':3,'4':4,'5':5,'6':6,'7':7,'8':8,'9':9}
KEY={'MENU':0xfc,'LEFT':0x10,'RIGHT':0x11,'UP':0x0b,'DOWN':0x0e,'OK':0x0d,'EXIT':0x20}
def keys_for(spec):
    ks=[];t=0
    for tok in spec.split():
        if tok=='W': t+=40000000; continue
        if tok in KEY: ks.append((t,KEY[tok])); t+=12000000
        else:
            for c in tok: ks.append((t,DIG[c])); t+=22000000
            t+=4000000
    return ks
def run(tag,spec,window=260000000,snapname=None,inject=None):
    global snap,INJECT
    snap_=snapname or snap; inj=inject or INJECT
    d=root+'work/rp/codes/'+tag+'/'; os.makedirs(d+'hist',exist_ok=True)
    ks=keys_for(spec)
    cmd=[exe]+base+['--headless','--summary',d+'s.json','--load-state',snap_,'--load-state-allow-extra-options','--exploratory-ge-line','--exploratory-provenance-extensions',
       '--max-instructions',str(inj+window),'--ir-script',','.join('%d:raw=0x%02x'%(inj+t,k) for t,k in ks),'--gop-frame-history',d+'hist','--watch-pc-regs','804c3684','--watch-guest-access','6d7db0+2','--watch-guest-access','6d7dc4+2','--watch-guest-access','6d7dd8+2','--exploratory-mvop-plane']
    subprocess.run(cmd,cwd=root,capture_output=True)
    try:
        s=json.load(open(d+'s.json'))
        osd=[h['a0'] for h in s.get('watched_pc_hits',{}).get('0x804c3684',{}).get('hits',[])]
        acc=[(e.get('pc'),e.get('last_value')) for e in s.get('watched_data_accesses',[]) if e.get('kind')=='store']
        print(tag,'|',spec,'|',s['reason'],'| OSDs',osd[-12:],'| stores',acc[-8:],flush=True)
    except Exception as e: print(tag,'FAILED',e,flush=True)
if __name__=='__main__':
    cases=[l.split('=',1) for l in sys.argv[1:]]
    import os as _o
    fm=_o.environ.get('FM')
    ths=[threading.Thread(target=run,args=(t,sp),kwargs=({'snapname':root+'work/checkpoints/factory-menu.snap','inject':1345000000,'window':200000000} if fm else {})) for t,sp in cases]
    [t.start() for t in ths]; [t.join() for t in ths]
