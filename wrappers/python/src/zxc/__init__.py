from ._zxc import (
    pyzxc_compress,
    pyzxc_decompress,
    pyzxc_stream_compress,
    pyzxc_stream_decompress,
)

__all__ = [
    "compress",
    "decompress",
    "stream_compress",
    "stream_decompress"
]

def compress(data, level = 3, checksum = False) -> bytes:
    """Compress a bytes object"""
    return pyzxc_compress(data, level, checksum)

def decompress(data, original_size, checksum = False) -> bytes:
    """Decompress a bytes object"""
    return pyzxc_decompress(data, original_size, checksum)

def stream_compress(src, dst, n_threads=0, level=3, checksum=False):
    """Compress data from src to dst (file-like objects)"""
    if not hasattr(src, "fileno") or not hasattr(dst, "fileno"):
        raise ValueError("src and dst must be open file-like objects")
    
    if not src.readable():
        raise ValueError("Source file must be readable")
    
    if not dst.writable():
        raise ValueError("Destination file must be writable")
    
    return pyzxc_stream_compress(src, dst, n_threads, level, checksum)

def stream_decompress(src, dst, n_threads=0, checksum=False):
    """Decompress data from src to dst (file-like objects)"""
    if not hasattr(src, "fileno") or not hasattr(dst, "fileno"):
        raise ValueError("src and dst must be open file-like objects")
    
    if not src.readable():
        raise ValueError("Source file must be readable")
    
    if not dst.writable():
        raise ValueError("Destination file must be writable")
    
    return pyzxc_stream_decompress(src, dst, n_threads, checksum)