"""Reusable compression and decompression contexts."""

import pytest

import zxc


def _samples(n=64):
    return [
        b'{"id":%d,"user":"alice_%d","mail":"a%d@example.com","role":"member"}'
        % (i, i, i)
        for i in range(n)
    ]


def test_roundtrip_matches_one_shot():
    payload = b"".join(_samples())
    with zxc.Cctx(level=zxc.LEVEL_DEFAULT) as cctx, zxc.Dctx() as dctx:
        archive = cctx.compress(payload)
        assert archive == zxc.compress(payload, level=zxc.LEVEL_DEFAULT)
        assert dctx.decompress(archive) == payload


def test_context_is_reusable():
    payloads = _samples(16)
    with zxc.Cctx() as cctx, zxc.Dctx() as dctx:
        archives = [cctx.compress(p) for p in payloads]
        assert [dctx.decompress(a) for a in archives] == payloads


def test_checksum_is_honoured():
    payload = b"".join(_samples())
    with zxc.Cctx(checksum=True) as cctx:
        archive = cctx.compress(payload)
    assert archive == zxc.compress(payload, checksum=True)
    with zxc.Dctx(checksum=True) as dctx:
        assert dctx.decompress(archive) == payload

    comp_size = int.from_bytes(archive[19:23], "little")
    at = 16 + 8 + comp_size
    assert at + 4 < len(archive), "expected a single-block archive"
    corrupt = bytearray(archive)
    corrupt[at] ^= 0xFF
    corrupt = bytes(corrupt)

    with zxc.Dctx(checksum=True) as dctx:
        with pytest.raises(RuntimeError, match="ZXC_ERROR_BAD_CHECKSUM"):
            dctx.decompress(corrupt)
    with zxc.Dctx(checksum=False) as dctx:
        assert dctx.decompress(corrupt) == payload


def test_dictionary_applies_to_every_call():
    samples = _samples()
    dictionary = zxc.train_dict(samples)
    table = zxc.train_dict_huf(samples, dictionary)
    payload = samples[7]

    with zxc.Cctx(dict=dictionary, dict_huf=table) as cctx:
        first = cctx.compress(payload)
        second = cctx.compress(payload)
    assert first == second
    assert first == zxc.compress(payload, dict=dictionary, dict_huf=table)
    assert zxc.get_dict_id(first) != 0
    assert len(first) < len(zxc.compress(payload))

    with zxc.Dctx(dict=dictionary, dict_huf=table) as dctx:
        assert dctx.decompress(first) == payload

    # The archive binds the dictionary: decoding without it must fail.
    with zxc.Dctx() as dctx, pytest.raises(RuntimeError):
        dctx.decompress(first)


def test_bad_table_length_is_rejected():
    with pytest.raises(ValueError):
        zxc.Cctx(dict=b"x" * 64, dict_huf=b"short")
    with pytest.raises(ValueError):
        zxc.Dctx(dict=b"x" * 64, dict_huf=b"short")


def test_use_after_close():
    cctx = zxc.Cctx()
    archive = cctx.compress(b"payload" * 100)
    cctx.close()
    cctx.close()  # idempotent
    with pytest.raises(ValueError):
        cctx.compress(b"again")

    dctx = zxc.Dctx()
    assert dctx.decompress(archive) == b"payload" * 100
    dctx.close()
    with pytest.raises(ValueError):
        dctx.decompress(archive)


def test_accepts_a_dictionary_object():
    samples = _samples()
    d = zxc.Dictionary.train(samples)
    payload = samples[7]
    with zxc.Cctx(dict=d) as cctx:
        archive = cctx.compress(payload)
    assert archive == zxc.compress(payload, dict=d)
    assert zxc.get_dict_id(archive) == d.id
    with zxc.Dctx(dict=d) as dctx:
        assert dctx.decompress(archive) == payload


def test_empty_payload_matches_the_one_shot():
    with zxc.Cctx() as cctx, zxc.Dctx() as dctx:
        archive = cctx.compress(b"")
        assert archive == zxc.compress(b"")
        assert dctx.decompress(archive) == b""


def test_rejects_a_multi_byte_buffer():
    import array

    with zxc.Cctx() as cctx, pytest.raises(TypeError):
        cctx.compress(array.array("i", [1, 2, 3]))


def test_table_without_dictionary_is_still_checked():
    with pytest.raises(ValueError):
        zxc.Cctx(dict_huf=b"short")
    with pytest.raises(ValueError):
        zxc.Dctx(dict_huf=b"short")


def test_oversized_dictionary_is_rejected_at_creation():
    for ctx in (zxc.Cctx, zxc.Dctx):
        with pytest.raises(RuntimeError, match="ZXC_ERROR_DICT_TOO_LARGE"):
            ctx(dict=b"x" * 65536)
        ctx(dict=b"x" * 65535).close()
