import os
import argparse
import pandas as pd
import numpy as np
from pathlib import Path
from platosim.utilities import errorcode

parser = argparse.ArgumentParser(epilog=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
parser.add_argument('ifile', type=str, help='Input file [ascii, csv]')
parser.add_argument('odir',  type=str, help='Output directory')
parser.add_argument('--fullframe',  action='store_true', help='Save to full-frame format')
args = parser.parse_args()

# I/O files
ifile = Path(args.ifile)
odir  = Path(args.odir)
filename = ifile.stem
suffix = ifile.suffix
columns = ['ra', 'dec', 'mag', 'ID']

# Check if file cat be opened
if suffix == '.csv':
    try:
        df = pd.read_csv(ifile)
    except:
        errorcode('error', f'Cannot read {filename}.csv: Is this a proper csv file?')
        
elif suffix == '.ftr':
    try:
        df = pd.read_feather(ifile)
    except:
        errorcode('error', f'Cannot read {filename}.ftr: Is this a proper feather file?')

else:
    # Try ascii ('.txt', '.dat', '.data', '.cat', etc.)
    try: 
        df = pd.read_csv(ifile, sep=' ', names=columns)
    except:
        errorcode('error', 'File cannot be read by pd.read_csv() -> Uncommented text?')
    else:
        if df.shape[0] > df.shape[1]:
            try: 
                df = pd.read_csv(ifile, sep='  ', names=columns)
            except:
                errorcode('error', 'File must contain 4 columns: {ra, dec, mag, ID}')
            else:
                if df.shape[1] != 4: 
                    errorcode('error', 'File column separation type is not recognised: ' +
                          'Use 1-2 delimiter separation spaces!')
                else:
                    pass

# Convert df
df.ID = df.ID.astype(int)

# Check which field in {LOPS2, LOPN1}
# TODO

# Output filename
if args.fullframe:
    ofile = odir / f'starcat_{filename}_group0.ftr'
else:
    ofile = odir / f'starcat_{filename}_targets.ftr'

# Save file
df.to_feather(ofile)
errorcode('message', f'All done! File saved: {ofile}')
