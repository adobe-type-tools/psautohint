# Copyright 2014,2021 Adobe. All rights reserved.

"""
Auto-hinting program for PostScript, OpenType/CFF and UFO fonts.
"""

import logging
import logging.handlers

class DuplicateMessageFilter(logging.Filter):
    """
    Suppresses any log message that was reported before in the same module and
    for the same logging level. We check for module and level number in
    addition to the message just in case, though checking the message only is
    probably enough.
    """

    def __init__(self):
        super(DuplicateMessageFilter, self).__init__()
        self.logs = set()

    def filter(self, record):
        current = (record.module, record.levelno, record.getMessage())
        if current in self.logs:
            return False
        self.logs.add(current)
        return True

def logging_conf(verbose, logfile=None, handlers=None):
    log_format = "%(levelname)s: %(message)s"
    if verbose == 0:
        log_level = logging.WARNING
    else:
        log_format = "[%(filename)s:%(lineno)d] " + log_format
        if verbose == 1:
            log_level = logging.INFO
        else:
            log_level = logging.DEBUG

    if handlers is not None:
        logging.basicConfig(format=log_format, level=log_level,
                            handlers=handlers)
    else:
        logging.basicConfig(format=log_format, level=log_level,
                            filename=logfile)

    # Filter duplicate logging messages only when not running the tests
    # and when not reporting more detailed log levels
    if log_level == logging.WARNING:
        for handler in logging.root.handlers:
            handler.addFilter(DuplicateMessageFilter())

def log_receiver(logQueue):
    while True:
        record = logQueue.get()
        if record is None:
            break
        logger = logging.getLogger(record.name)
        logger.handle(record)

def logging_reconfig(logQueue, verbose=0):
    qh = logging.handlers.QueueHandler(logQueue)
    root = logging.getLogger()
    if root.handlers:
        # Already configured logging to just swap out handlers
        for h in root.handlers:
            root.removeHandler(h)
        qh.addFilter(DuplicateMessageFilter())
        root.addHandler(qh)
    else:
        logging_conf(verbose, None, [qh])
