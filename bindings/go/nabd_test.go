package nabd

import (
	"testing"
)

const TestQueue = "/nabd_go_test"

func TestOpenClose(t *testing.T) {
	Unlink(TestQueue)
	defer Unlink(TestQueue)

	q, err := Open(TestQueue, 16, 64, Create|Producer)
	if err != nil {
		t.Fatalf("Open failed: %v", err)
	}
	q.Close()
}

func TestPushPop(t *testing.T) {
	Unlink(TestQueue)
	defer Unlink(TestQueue)

	p, err := Open(TestQueue, 16, 64, Create|Producer)
	if err != nil {
		t.Fatalf("Producer open failed: %v", err)
	}
	defer p.Close()

	c, err := Open(TestQueue, 0, 0, Consumer)
	if err != nil {
		t.Fatalf("Consumer open failed: %v", err)
	}
	defer c.Close()

	msg := []byte("Hello Go")
	if err := p.Push(msg); err != nil {
		t.Errorf("Push failed: %v", err)
	}

	out, err := c.Pop(128)
	if err != nil {
		t.Errorf("Pop failed: %v", err)
	}

	if string(out) != string(msg) {
		t.Errorf("Expected %s, got %s", msg, out)
	}

	// Empty check
	_, err = c.Pop(128)
	if err != ErrEmpty {
		t.Errorf("Expected ErrEmpty, got %v", err)
	}
}
