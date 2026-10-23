"""Prepare an explicit source-space declaration. Never writes the active identity/sidecar."""
import argparse,hashlib,json,uuid
from pathlib import Path
def prepare(identity,sender,continuity,generation,output):
    raw=identity.read_bytes();lines=raw.decode('utf-8').splitlines()
    if len(lines) not in (4,6) or lines[0] not in ('pico-observation-identity-v1','pico-observation-identity-v2'):raise ValueError('Invalid identity state')
    if not 0<sender<2**64:raise ValueError('Sender session must be the current receiver log value')
    previous=lines[4] if len(lines)==6 else ''
    if continuity=='same':
        if not previous or (generation and generation!=previous):raise ValueError('Same requires the existing explicit generation')
        generation=previous
    elif continuity=='changed':
        generation=generation or str(uuid.uuid4())
        if generation==previous:raise ValueError('Changed requires a different generation')
    else:generation=previous or generation or 'unverified'
    if not generation or len(generation)>128 or '\n' in generation or '\r' in generation:raise ValueError('Invalid generation')
    active=Path(str(identity)+'.space')
    if output.resolve() in (identity.resolve(),active.resolve()):raise ValueError('Output must be a separate reviewed candidate')
    data='\n'.join(['pico-source-space-declaration-v1',lines[1],lines[2],generation,str(sender),'unknown' if continuity=='unknown' else 'known'])+'\n'
    with output.open('x',encoding='utf-8',newline='\n') as f:f.write(data)
    return dict(status='PREPARED_NOT_APPLIED',source=lines[1],space=lines[2],continuity=continuity,generation=generation,sender_session=str(sender),identity_sha256=hashlib.sha256(raw).hexdigest(),candidate_sha256=hashlib.sha256(output.read_bytes()).hexdigest(),candidate=str(output),destination=str(active))
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--identity',type=Path,required=True);p.add_argument('--sender-session',type=int,required=True);p.add_argument('--continuity',choices=['same','changed','unknown'],required=True);p.add_argument('--generation');p.add_argument('--output',type=Path,required=True);a=p.parse_args();print(json.dumps(prepare(a.identity,a.sender_session,a.continuity,a.generation,a.output),indent=2))
if __name__=='__main__':main()
