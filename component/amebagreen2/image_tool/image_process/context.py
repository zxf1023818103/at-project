from abc import ABC
import os
import logging
import json5
import json
from utility import *

class Context(ABC):
    manifest_file:str
    manifest_data:dict
    soc_project:str
    mcu_project:str

    logger:logging.Logger

    def __init__(self, args, object) -> None:
        self.args = args
        self.logger = create_logger("imagetool", getattr(logging, args.log_level.upper()))

        self.image_folder_name = 'image_mp' if args.mp == 'y' else 'image'
        self.image_output_dir = ''

        # Standalone Green2 port: configuration belongs to the explicit project
        # or signing directory. Do not discover an external SDK or SoC map.
        if args.extern_dir is None:
            raise ValueError('--extern-dir is required for the standalone Green2 packer')
        self.external_soc_dir = os.path.abspath(args.extern_dir)
        with open(os.path.join(self.external_soc_dir, 'soc_info.json'), 'r') as stream:
            soc_info = json.load(stream)
        if soc_info.get('soc', {}).get('name') != 'RTL8711F':
            raise ValueError('Expected RTL8711F project metadata')
        self.soc_dir = self.external_soc_dir
        self.soc_project = 'amebagreen2'
        self.mcu_project = ''

        if object.require_manifest_file(self):
            self.manifest_file = os.path.join(self.external_soc_dir, 'manifest.json5')
            if not os.path.exists(self.manifest_file):
                self.logger.fatal("Failed get manifest file for current project")
                raise ValueError
            with open(self.manifest_file, 'r') as f:
                self.manifest_data = json5.load(f)

        if object.require_layout_file(self):
            self.layout_file = os.path.join(self.soc_dir, 'ameba_layout.ld')
            if not os.path.exists(self.layout_file):
                self.logger.fatal("Failed get layout file for current project")
                raise ValueError
