import unittest
import time
import os
from nabd import Nabd, NABD_CREATE, NABD_PRODUCER, NABD_CONSUMER, NABD_OK, NABD_FULL, NABD_EMPTY

QUEUE_NAME = "/nabd_unittest"

class TestNabd(unittest.TestCase):
    def setUp(self):
        try:
            Nabd.unlink(QUEUE_NAME)
        except OSError:
            pass

    def tearDown(self):
        try:
            Nabd.unlink(QUEUE_NAME)
        except OSError:
            pass

    def test_open_close(self):
        q = Nabd(QUEUE_NAME, 32, 64, NABD_CREATE | NABD_PRODUCER)
        self.assertIsNotNone(q._handle)
        q.close()
        self.assertIsNone(q._handle)

    def test_push_pop(self):
        p = Nabd(QUEUE_NAME, 16, 64, NABD_CREATE | NABD_PRODUCER)
        c = Nabd(QUEUE_NAME, 0, 0, NABD_CONSUMER)
        
        msg = b"Unit Test Message"
        self.assertEqual(p.push(msg), NABD_OK)
        
        popped = c.pop()
        self.assertEqual(popped, msg)
        
        # Second pop should be empty
        self.assertIsNone(c.pop())
        
        p.close()
        c.close()

    def test_full(self):
        capacity = 4
        q = Nabd(QUEUE_NAME, capacity, 64, NABD_CREATE | NABD_PRODUCER | NABD_CONSUMER)
        
        # Fill it up
        for i in range(capacity):
            res = q.push(f"msg-{i}")
            self.assertEqual(res, NABD_OK)
            
        # Push to full
        res = q.push("overflow")
        self.assertEqual(res, NABD_FULL)
        
        # Drain one
        popped = q.pop()
        self.assertEqual(popped, b"msg-0")
        
        # Now can push again
        res = q.push("new")
        self.assertEqual(res, NABD_OK)
        
        q.close()

if __name__ == '__main__':
    unittest.main()
